#include "tcp_rttp_bridge.h"
#include "tcp_server.h"

#include <random>
#include <iostream>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <chrono>


tcp_rttp_bridge::tcp_rttp_bridge(RTEngine engine, RTSOCKET upstream_socket, uv_tcp_t* downstream_socket, uv_loop_t* loop, const bridge_param& param, tcp_server* server)
	:
	loop_(loop),
	rttp_engine_(engine),
	downstream_tcp_socket_(downstream_socket),
	upstream_rttp_socket_(upstream_socket),
	param_(param),
	server_(server)
{
	downstream_tcp_socket_->data = this;
}

tcp_rttp_bridge::~tcp_rttp_bridge()
{
    if (downstream_tcp_socket_) {
        free(downstream_tcp_socket_);
    }
}

void tcp_rttp_bridge::on_downstream_connected()
{
	
	auto seed = std::chrono::high_resolution_clock::now().time_since_epoch().count();
	std::mt19937 rng(seed);
	std::uniform_int_distribution<int> dist(0, param_.upstream_address.size() - 1);
	int index = dist(rng);

	// Get selected upstream server address and port
	const auto& upstream = param_.upstream_address[index];
	std::string host = upstream.first;
	int port = upstream.second;

	std::cout << "Connecting to upstream RTTP server: " << host << ":" << port << std::endl;

    // Async DNS resolution
	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	resolver_req_.data = this;
	int result = uv_getaddrinfo(loop_, &resolver_req_, on_resolved, 
	                            host.c_str(), std::to_string(port).c_str(), &hints);
	
	if (result != 0) {
		std::cerr << "Failed to start DNS resolution: " << uv_strerror(result) << std::endl;
		close();
	}
}


void tcp_rttp_bridge::close()
{
	
	if (closed_) 
		return;

	closed_ = true;
	
	std::cout << "Closing bridge connections" << std::endl;
	
    if (downstream_tcp_socket_ && !uv_is_closing((uv_handle_t*)downstream_tcp_socket_)) {
        pending_close_handles_++;
        uv_close((uv_handle_t*)downstream_tcp_socket_, on_tcp_close);
    }
	
	if (upstream_rttp_socket_ != 0) {

		rt_set_userdata(rttp_engine_, upstream_rttp_socket_, 1, NULL);
		rt_close(rttp_engine_, upstream_rttp_socket_);
		
		upstream_rttp_socket_ = NULL;
	}

	if (server_) {
		server_->on_connection_closed();
	}
	
    if (pending_close_handles_ == 0) {
        delete this;
    }
}

void tcp_rttp_bridge::do_rttp_recv()
{
	
	if (tcp_writting_)
		return;

	// Reset accumulated buffer
	accumulated_data_size_ = 0;

	// Loop to read all available data from RTTP socket into accumulated buffer
	while (true) {

		// Check if accumulated buffer has space
		if (accumulated_data_size_ + max_length > accumulated_buffer_size) {
			// Buffer full, write what we have
			break;
		}

        int max_read = (int)max_length;
		if (max_read > (int)(accumulated_buffer_size - accumulated_data_size_)) {
			max_read = (int)(accumulated_buffer_size - accumulated_data_size_);
		}

		int bytes_read = rt_recv(rttp_engine_, upstream_rttp_socket_, 
			accumulated_buffer_ + accumulated_data_size_, 
			max_read, 0);
	
		if (bytes_read > 0)
		{
			accumulated_data_size_ += bytes_read;
			// Continue reading more data
		}
		else if (bytes_read == 0)
		{
			std::cout << "Upstream RTTP connection closed by peer" << std::endl;
			close();
			return;
		}
		else
		{
			// No more data available (rt_recv returned -1), exit loop
			break;
		}
	}

	// If we have accumulated data, write it all at once
	if (accumulated_data_size_ > 0)
	{
		tcp_writting_ = true;
        
		write_req_.data = this;
		uv_buf_t buf = uv_buf_init(accumulated_buffer_, accumulated_data_size_);

		uv_write(&write_req_, (uv_stream_t*)downstream_tcp_socket_, &buf, 1, on_tcp_write);
	}
}

void tcp_rttp_bridge::do_tcp_recv()
{
	if (tcp_reading_)
		return;

	tcp_reading_ = true;
    uv_read_start((uv_stream_t*)downstream_tcp_socket_, alloc_buffer, on_tcp_read);
}


void tcp_rttp_bridge::do_rttp_send()
{
	while (tcp_data_size_ > 0) {
		int bytes_send = rt_send(rttp_engine_, upstream_rttp_socket_, tcp_buffer_ + tcp_data_pos_, tcp_data_size_, 0);

		if (bytes_send > 0)
		{
			tcp_data_pos_ += bytes_send;
			tcp_data_size_ -= bytes_send;
		}
		else {
			break;
		}
	}

	if (tcp_data_size_ == 0) {
		do_tcp_recv();
	}
}

void tcp_rttp_bridge::on_upstream_connected()
{
	do_tcp_recv();
}

void tcp_rttp_bridge::on_upstream_readable()
{
	do_rttp_recv();
}

void tcp_rttp_bridge::on_upstream_writable()
{
	do_rttp_send();
}

void tcp_rttp_bridge::on_upstream_error()
{
	close();
}

// libuv callbacks
void tcp_rttp_bridge::on_resolved(uv_getaddrinfo_t* req, int status, struct addrinfo* res)
{
    tcp_rttp_bridge* bridge = (tcp_rttp_bridge*)req->data;

	if (status != 0) {
		std::cerr << "Failed to resolve upstream host: " << uv_strerror(status) << std::endl;
		bridge->close();
		return;
	}

    if (res->ai_family == AF_INET) {
        struct sockaddr_in* addr = (struct sockaddr_in*)res->ai_addr;
        rt_connect(bridge->rttp_engine_, bridge->upstream_rttp_socket_, (const struct sockaddr*)addr, sizeof(struct sockaddr_in));
    } else if (res->ai_family == AF_INET6) {
        struct sockaddr_in6* addr = (struct sockaddr_in6*)res->ai_addr;
        rt_connect(bridge->rttp_engine_, bridge->upstream_rttp_socket_, (const struct sockaddr*)addr, sizeof(struct sockaddr_in6));
    }

	uv_freeaddrinfo(res);
}

void tcp_rttp_bridge::alloc_buffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf)
{
    buf->base = (char*)malloc(suggested_size);
	buf->len = suggested_size;
}

void tcp_rttp_bridge::on_tcp_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf)
{
    tcp_rttp_bridge* bridge = (tcp_rttp_bridge*)stream->data;

	if (nread > 0) {
		bridge->tcp_data_pos_ = 0;
		bridge->tcp_data_size_ = nread;
        
        // Ensure buffer size
        if (nread > bridge->max_length) {
             // This shouldn't happen if we control alloc_buffer, but for safety
             std::cerr << "Read too much data" << std::endl;
             bridge->close();
             free(buf->base);
             return;
        }

		memcpy(bridge->tcp_buffer_, buf->base, nread);
		bridge->tcp_reading_ = false;
		
		uv_read_stop(stream);
		
		bridge->do_rttp_send();
	}
	else if (nread < 0) {
		bridge->tcp_reading_ = false;
		
		if (nread == UV_EOF) {
			std::cout << "Downstream TCP connection closed by peer" << std::endl;
		}
		else {
			std::cerr << "Error reading from downstream TCP connection: " << uv_strerror(nread) << std::endl;
		}
		bridge->close();
	}

	if (buf->base) {
		free(buf->base);
	}
}

void tcp_rttp_bridge::on_tcp_write(uv_write_t* req, int status)
{
	tcp_rttp_bridge* bridge = (tcp_rttp_bridge*)req->data;

	bridge->tcp_writting_ = false;

	if (status != 0) {
		std::cerr << "Failed to write to downstream TCP connection: " << uv_strerror(status) << std::endl;
		bridge->close();
		return;
	}

	bridge->do_rttp_recv();
}

void tcp_rttp_bridge::on_tcp_close(uv_handle_t* handle)
{
    tcp_rttp_bridge* bridge = (tcp_rttp_bridge*)handle->data;
	
	bridge->pending_close_handles_--;
	
	if (bridge->pending_close_handles_ == 0) {
		delete bridge;
	}
}