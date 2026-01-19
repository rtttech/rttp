#include "rttp_tcp_bridge.h"
#include "rttp_server.h"

#include <iostream>
#include <random>
#include <sstream>
#include <cstring>
#include <algorithm>



uint64_t rttp_tcp_bridge::next_connection_id_ = 0;

rttp_tcp_bridge::rttp_tcp_bridge(RTEngine engine, RTSOCKET downstream_socket, uv_loop_t* loop, const bridge_param& param, rttp_server* server, stats_manager* stats_mgr, http_monitor* http_monitor)
	:
	loop_(loop),
	rttp_engine_(engine),
	downstream_rttp_socket_(downstream_socket),
	param_(param),
	server_(server),
	stats_mgr_(stats_mgr),
	http_monitor_(http_monitor),
	last_stats_flush_time_(time(NULL))
{

	connection_id_ = std::to_string(next_connection_id_++);

	// Initialize TCP socket
	uv_tcp_init(loop_, &upstream_tcp_socket_);
	upstream_tcp_socket_.data = this;
	tcp_handle_initialized_ = true;
}

rttp_tcp_bridge::~rttp_tcp_bridge()
{
	std::cout << "rttp_tcp_bridge::~rttp_tcp_bridge" << std::endl;
}

void rttp_tcp_bridge::on_downstream_connected()
{
	
	auto seed = std::chrono::high_resolution_clock::now().time_since_epoch().count();
	std::mt19937 rng(seed);
	std::uniform_int_distribution<int> dist(0, param_.upstream_address.size() - 1);
	int index = dist(rng);

	// Get selected upstream server address and port
	const auto& upstream = param_.upstream_address[index];
	std::string host = upstream.first;
	int port = upstream.second;

	std::cout << "Connecting to upstream server: " << host << ":" << port << std::endl;

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


void rttp_tcp_bridge::close()
{
	
	if (closed_) 
		return;

	closed_ = true;
	
	std::cout << "Closing bridge connections" << std::endl;
	
	// Close TCP connection
	if (tcp_handle_initialized_ && !uv_is_closing((uv_handle_t*)&upstream_tcp_socket_)) {
		pending_close_handles_++;
		uv_close((uv_handle_t*)&upstream_tcp_socket_, on_tcp_handle_closed);
	}

	// Flush final stats before closing
	if (stats_mgr_ && http_monitor_) {
		if (local_bytes_sent_ > 0 || local_bytes_received_ > 0) {
			std::string id = connection_id_;
			uint64_t sent = local_bytes_sent_;
			uint64_t recv = local_bytes_received_;
			http_monitor_->post_task([mgr=stats_mgr_, id, sent, recv]() {
				if (mgr) mgr->on_data_transferred(id, sent, recv);
			});
			local_bytes_sent_ = 0;
			local_bytes_received_ = 0;
		}

		// Update final RTTP stats
		if (downstream_rttp_socket_) {
			int64_t rtt = 0;
			int len = sizeof(rtt);
			rt_getsockopt(rttp_engine_, downstream_rttp_socket_, RTSO_RTT, &rtt, len);

			int32_t loss_rate = 0;
			len = sizeof(loss_rate);
			rt_getsockopt(rttp_engine_, downstream_rttp_socket_, RTSO_LOST_RATE, &loss_rate, len);

			std::string id = connection_id_;
			double rtt_val = (double)rtt / 1000.0;
			double loss_val = (double)loss_rate;
			http_monitor_->post_task([mgr=stats_mgr_, id, rtt_val, loss_val]() {
				if (mgr) mgr->update_rttp_stats(id, rtt_val, loss_val);
			});
		}

		std::string id = connection_id_;
		http_monitor_->post_task([mgr=stats_mgr_, id]() {
			if (mgr) mgr->on_connection_closed(id);
		});
	}
	
	if (downstream_rttp_socket_ != 0) {

		rt_set_userdata(rttp_engine_, downstream_rttp_socket_, 1, NULL);
		rt_close(rttp_engine_, downstream_rttp_socket_);
		
		downstream_rttp_socket_ = NULL;
	}
	
	upstream_connected_ = false;
	
	if (server_) {
		server_->on_connection_closed();
	}
	
	if (pending_close_handles_ == 0) {
		delete this;
	}
}

void rttp_tcp_bridge::do_rttp_recv()
{

	if (tcp_writting_)
		return;

	// Reset accumulated buffer
	accumulated_data_size_ = 0;

	// Loop to read all available data from RTTP socket into accumulated buffer
	while (upstream_connected_) {

		// Check if accumulated buffer has space
		if (accumulated_data_size_ + max_length > accumulated_buffer_size) {
			// Buffer full, write what we have
			break;
		}

		int max_read = (int)max_length;
		if (max_read > (int)(accumulated_buffer_size - accumulated_data_size_)) {
			max_read = (int)(accumulated_buffer_size - accumulated_data_size_);
		}
		
		int bytes_read = rt_recv(rttp_engine_, downstream_rttp_socket_, 
			accumulated_buffer_ + accumulated_data_size_, 
			max_read, 0);

		if (bytes_read > 0)
		{
			accumulated_data_size_ += bytes_read;
			// Local accumulation instead of cross-thread call
			local_bytes_received_ += bytes_read;
			// Continue reading more data
		}
		else if (bytes_read == 0)
		{
			// RTTP连接已关闭
			std::cout << "Downstream RTTP connection closed by peer" << std::endl;
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
	if (accumulated_data_size_ > 0 && upstream_connected_)
	{
		try_flush_stats();

		tcp_writting_ = true;
		
		write_req_.data = this;
		uv_buf_t buf = uv_buf_init(accumulated_buffer_, accumulated_data_size_);

		uv_write(&write_req_, (uv_stream_t*)&upstream_tcp_socket_, &buf, 1, on_tcp_write);
	}
}

void rttp_tcp_bridge::do_tcp_recv()
{
	if (tcp_reading_)
		return;

	tcp_reading_ = true;
	uv_read_start((uv_stream_t*)&upstream_tcp_socket_, alloc_buffer, on_tcp_read);
}

void rttp_tcp_bridge::on_downstream_readable()
{
	do_rttp_recv();
}

void rttp_tcp_bridge::on_downstream_writable()
{
	do_rttp_send();
}

void rttp_tcp_bridge::on_downstream_error()
{
	close();
}

void rttp_tcp_bridge::do_rttp_send()
{
	while (tcp_data_size_ > 0) {
		int bytes_send = rt_send(rttp_engine_, downstream_rttp_socket_, tcp_buffer_ + tcp_data_pos_, tcp_data_size_, 0);

		if (bytes_send > 0)
		{
			tcp_data_pos_ += bytes_send;
			tcp_data_size_ -= bytes_send;
			// Local accumulation instead of cross-thread call
			local_bytes_sent_ += bytes_send;
		}
		else {
			break;
		}
	}

	if (tcp_data_size_ == 0) {
		do_tcp_recv();
	}
}

void rttp_tcp_bridge::try_flush_stats()
{
	if (closed_)
		return;

	auto time_now = time(NULL);
	auto elapsed =  time_now - last_stats_flush_time_;

	// Only flush if at least 3 seconds have passed
	if (elapsed >= 3) {
		// Flush accumulated data transfer stats
		if (local_bytes_sent_ > 0 || local_bytes_received_ > 0) {
			std::string id = connection_id_;
			uint64_t sent = local_bytes_sent_;
			uint64_t recv = local_bytes_received_;
			
			if (http_monitor_) {
				http_monitor_->post_task([mgr=stats_mgr_, id, sent, recv]() {
					if (mgr) mgr->on_data_transferred(id, sent, recv);
				});
			}
			
			local_bytes_sent_ = 0;
			local_bytes_received_ = 0;
		}

		// Update RTTP connection quality stats
		if (downstream_rttp_socket_) {
			int64_t rtt = 0;
			int len = sizeof(rtt);
			rt_getsockopt(rttp_engine_, downstream_rttp_socket_, RTSO_RTT, &rtt, len);

			int32_t loss_rate = 0;
			len = sizeof(loss_rate);
			rt_getsockopt(rttp_engine_, downstream_rttp_socket_, RTSO_LOST_RATE, &loss_rate, len);

			std::string id = connection_id_;
			double rtt_val = (double)rtt / 1000.0;
			double loss_val = (double)loss_rate;
			
			if (http_monitor_) {
				http_monitor_->post_task([mgr=stats_mgr_, id, rtt_val, loss_val]() {
					if (mgr) mgr->update_rttp_stats(id, rtt_val, loss_val);
				});
			}
		}

		last_stats_flush_time_ = time_now;
	}
}

// libuv callbacks
void rttp_tcp_bridge::alloc_buffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf)
{
	buf->base = (char*)malloc(suggested_size);
	buf->len = suggested_size;
}

void rttp_tcp_bridge::on_resolved(uv_getaddrinfo_t* req, int status, struct addrinfo* res)
{
	rttp_tcp_bridge* bridge = (rttp_tcp_bridge*)req->data;

	if (status != 0) {
		std::cerr << "Failed to resolve upstream host: " << uv_strerror(status) << std::endl;
		bridge->close();
		return;
	}

	uv_connect_t* connect_req = (uv_connect_t*)malloc(sizeof(uv_connect_t));
	connect_req->data = bridge;

	bridge->tcp_connecting_ = true;
	int result = uv_tcp_connect(connect_req, &bridge->upstream_tcp_socket_, res->ai_addr, on_tcp_connect);
	
	if (result != 0) {
		std::cerr << "Failed to start TCP connection: " << uv_strerror(result) << std::endl;
		bridge->tcp_connecting_ = false;
		free(connect_req);
		bridge->close();
	}

	uv_freeaddrinfo(res);
}

void rttp_tcp_bridge::on_tcp_connect(uv_connect_t* req, int status)
{
	rttp_tcp_bridge* bridge = (rttp_tcp_bridge*)req->data;
	free(req);

	bridge->tcp_connecting_ = false;

	// Check if bridge was already closed
	if (bridge->closed_) {
		std::cout << "Bridge already closed, ignoring TCP connect callback" << std::endl;
		return;
	}

	if (status != 0) {
		std::cerr << "Failed to connect to upstream server: " << uv_strerror(status) << std::endl;
		bridge->close();
		return;
	}

	std::cout << "Successfully connected to upstream server" << std::endl;
	bridge->upstream_connected_ = true;

	uv_tcp_nodelay(&bridge->upstream_tcp_socket_, 1);

	bridge->do_rttp_recv();
	bridge->do_tcp_recv();

	// Register connection with stats manager
	if (bridge->stats_mgr_) {
		std::string rttp_addr = "unknown";
		struct sockaddr_storage sa;
		int len = sizeof(sa);
		if (rt_getpeername(bridge->rttp_engine_, bridge->downstream_rttp_socket_, (struct sockaddr*)&sa, len) > 0) {
			char ip[INET6_ADDRSTRLEN];
			int port = 0;
			
			if (sa.ss_family == AF_INET) {
				struct sockaddr_in* sin = (struct sockaddr_in*)&sa;
				uv_ip4_name(sin, ip, sizeof(ip));
				port = ntohs(sin->sin_port);
			} else if (sa.ss_family == AF_INET6) {
				struct sockaddr_in6* sin6 = (struct sockaddr_in6*)&sa;
				uv_ip6_name(sin6, ip, sizeof(ip));
				port = ntohs(sin6->sin6_port);
			}
			
			rttp_addr = std::string(ip) + ":" + std::to_string(port);
		}

		struct sockaddr_storage tcp_sa;
		int tcp_len = sizeof(tcp_sa);
		std::string tcp_addr = "unknown";
		if (uv_tcp_getpeername(&bridge->upstream_tcp_socket_, (struct sockaddr*)&tcp_sa, &tcp_len) == 0) {
			char ip[INET6_ADDRSTRLEN];
			int port = 0;
			
			if (tcp_sa.ss_family == AF_INET) {
				struct sockaddr_in* sin = (struct sockaddr_in*)&tcp_sa;
				uv_ip4_name(sin, ip, sizeof(ip));
				port = ntohs(sin->sin_port);
			} else if (tcp_sa.ss_family == AF_INET6) {
				struct sockaddr_in6* sin6 = (struct sockaddr_in6*)&tcp_sa;
				uv_ip6_name(sin6, ip, sizeof(ip));
				port = ntohs(sin6->sin6_port);
			}
			
			tcp_addr = std::string(ip) + ":" + std::to_string(port);
		}

		std::string id = bridge->connection_id_;
		if (bridge->http_monitor_) {
			bridge->http_monitor_->post_task([mgr=bridge->stats_mgr_, id, rttp_addr, tcp_addr]() {
				if (mgr) mgr->on_connection_opened(id, rttp_addr, tcp_addr);
			});
		}
	}
}

void rttp_tcp_bridge::on_tcp_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf)
{
	rttp_tcp_bridge* bridge = (rttp_tcp_bridge*)stream->data;

	if (nread > 0) {
		bridge->tcp_data_pos_ = 0;
		bridge->tcp_data_size_ = nread;
		memcpy(bridge->tcp_buffer_, buf->base, nread);
		bridge->tcp_reading_ = false;
		
		uv_read_stop(stream);
		
		bridge->do_rttp_send();
	}
	else if (nread < 0) {
		bridge->tcp_reading_ = false;
		
		if (nread == UV_EOF) {
			std::cout << "Upstream TCP connection closed by peer" << std::endl;
		}
		else {
			std::cerr << "Error reading from upstream TCP connection: " << uv_strerror(nread) << std::endl;
		}
		bridge->close();
	}

	if (buf->base) {
		free(buf->base);
	}
}

void rttp_tcp_bridge::on_tcp_write(uv_write_t* req, int status)
{
	rttp_tcp_bridge* bridge = (rttp_tcp_bridge*)req->data;

	bridge->tcp_writting_ = false;

	if (status != 0) {
		std::cerr << "Failed to write to upstream TCP connection: " << uv_strerror(status) << std::endl;
		bridge->close();
		return;
	}

	bridge->do_rttp_recv();
}

void rttp_tcp_bridge::on_tcp_handle_closed(uv_handle_t* handle)
{
	rttp_tcp_bridge* bridge = (rttp_tcp_bridge*)handle->data;
	
	bridge->pending_close_handles_--;
	std::cout << "TCP handle closed, pending handles: " << bridge->pending_close_handles_ << std::endl;
	
	if (bridge->pending_close_handles_ == 0) {
		delete bridge;
	}
}

