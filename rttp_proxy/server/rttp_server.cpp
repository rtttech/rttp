#include "rttp_server.h"

#include <iostream>
#include <cstring>
#include <random>
#include <chrono>
#include <functional>
#include <memory>
#include <algorithm>
#include <string>


// UDP send request structure
struct udp_send_req_t {
	uv_udp_send_t req;
	uv_buf_t buf;
};

rttp_server::rttp_server(uv_loop_t* loop, const std::string local_host, short local_port, const bridge_param& param, short http_port)
	: loop_(loop), param_(param),
	rttp_engine_(NULL), cur_connections_num_(0),
	stats_mgr_(100), // Keep last 100 connections in history
	start_time_(std::chrono::steady_clock::now()),
	local_host_(local_host), local_port_(local_port), http_port_(http_port)
{
}

uint64_t rttp_server::get_uptime_seconds() const
{
	auto now = std::chrono::steady_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_);
	return duration.count();
}

rttp_server::~rttp_server()
{
	stop();
}


void rttp_server::init_rttp_engine()
{
	if (rttp_engine_ == NULL) {
		rttp_engine_ = rt_init("");
		rt_set_callback(rttp_engine_, on_rtsocket_event, packet_send_imp);
	}
}

void rttp_server::uninit_rttp_engine()
{
	if (rttp_engine_ != NULL) {
		rt_uninit(rttp_engine_);
		rttp_engine_ = NULL;
	}
}

void rttp_server::handle_timer()
{
	rt_tick(rttp_engine_);
}

void rttp_server::handle_udp_packet(const char* data, ssize_t nread, const struct sockaddr* addr)
{
	RTSOCKET socket = rt_incoming_packet(rttp_engine_, data, (int)nread, addr, sizeof(struct sockaddr_storage));
	if (socket != NULL) {
		std::cout << "incoming connection " << socket << std::endl;

		rt_set_userdata(rttp_engine_, socket, 0, this);

		if (cur_connections_num_ >= param_.max_connections) {
			std::cout << "max conn reached" << std::endl;
			rt_close(rttp_engine_, socket);
		}
		else {
			cur_connections_num_++;
			rt_accept(rttp_engine_, socket);
			on_incoming_rttp_connection(socket);
		}
	}
}

void rttp_server::on_incoming_rttp_connection(RTSOCKET socket)
{
	std::cout << "New RTTP connection accepted" << std::endl;

	rttp_tcp_bridge* bridge = new rttp_tcp_bridge(rttp_engine_, socket, loop_, param_, this, &stats_mgr_, http_monitor_.get());
	rt_set_userdata(rttp_engine_, socket, 1, bridge);

	bridge->on_downstream_connected();
}



void rttp_server::on_rtsocket_write(RTEngine engine, RTSOCKET socket)
{
	rttp_tcp_bridge* bridge_ptr = (rttp_tcp_bridge*)rt_get_userdata(engine, socket, 1);

	if (bridge_ptr != NULL) {
		bridge_ptr->on_downstream_writable();
	}
}


void rttp_server::on_rtsocket_read(RTEngine engine, RTSOCKET socket)
{
	rttp_tcp_bridge* bridge_ptr = (rttp_tcp_bridge*)rt_get_userdata(engine, socket, 1);

	if (bridge_ptr != NULL) {
		bridge_ptr->on_downstream_readable();
	}
}

void rttp_server::on_rtsocket_connect(RTEngine engine, RTSOCKET socket)
{
	//would not be called for rttp server
}


void rttp_server::on_rtsocket_error(RTEngine engine, RTSOCKET socket)
{

	rttp_tcp_bridge* bridge_ptr = (rttp_tcp_bridge*)rt_get_userdata(engine, socket, 1);

	if (bridge_ptr != NULL) {
		bridge_ptr->on_downstream_error();
	}

}

void rttp_server::on_rtsocket_event(RTEngine engine, RTSOCKET socket, int event)
{
	switch (event) {
	case RTTP_EVENT_CONNECT:
		on_rtsocket_connect(engine, socket);
		return;
	case RTTP_EVENT_READ:
		on_rtsocket_read(engine, socket);
		return;
	case RTTP_EVENT_WRITE:
		on_rtsocket_write(engine, socket);
		return;
	case RTTP_EVENT_ERROR:
		on_rtsocket_error(engine, socket);
		return;
	}
}

void rttp_server::packet_send_imp(RTEngine engine, RTSOCKET socket, const char* data, int len, const struct sockaddr* sa, int sock_len)
{
	rttp_server* server_ptr = (rttp_server*)rt_get_userdata(engine, socket, 0);

	uv_buf_t buf;
	buf.base = const_cast<char*>(data); 
	buf.len = len;

	int result = uv_udp_try_send(&server_ptr->udp_socket_, &buf, 1, sa);
	
	if (result < 0) {
    	std::cout<<"uv_udp_try_send return "<<result<<std::endl;
	}
    
}

// libuv callbacks
void rttp_server::alloc_udp_buffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf)
{
	
	if (uv_udp_using_recvmmsg((uv_udp_t*)handle))
		 suggested_size *= 64;

	char* buff_ptr = (char*)malloc(suggested_size);

	buf->len = suggested_size;
	buf->base = buff_ptr;
}

void rttp_server::on_udp_recv(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf, const struct sockaddr* addr, unsigned flags)
{
	rttp_server* server = (rttp_server*)handle->data;

	if (nread > 0 && addr != NULL) {
		server->handle_udp_packet(buf->base, nread, addr);
	}
	else if (nread < 0) {
		std::cerr << "UDP receive error: " << uv_strerror(nread) << std::endl;
	}

	if (buf != NULL && buf->base != NULL && !(flags & UV_UDP_MMSG_CHUNK) ) {
		free(buf->base);
	}
}

void rttp_server::on_timer(uv_timer_t* handle)
{
	rttp_server* server = (rttp_server*)handle->data;
	server->handle_timer();
}

void rttp_server::on_udp_send(uv_udp_send_t* req, int status)
{
	udp_send_req_t* send_req = (udp_send_req_t*)req;
	
	if (status != 0) {
		std::cout << "udp send failed: " << uv_strerror(status) << std::endl;
	}

	free(send_req->buf.base);
	free(send_req);
}


int rttp_server::start()
{
	if (run_)
		return 0;

	if (!http_monitor_) {
		http_monitor_ = std::make_shared<http_monitor>(http_port_, stats_mgr_, *this);
	}

	int result = uv_udp_init_ex(loop_, &udp_socket_, UV_UDP_RECVMMSG);
	if (result != 0) {
		std::cerr << "Failed to init UDP socket: " << uv_strerror(result) << std::endl;
		return result;
	}

	udp_socket_.data = this;

	struct sockaddr_in bind_addr;
	uv_ip4_addr(local_host_.c_str(), local_port_, &bind_addr);

	result = uv_udp_bind(&udp_socket_, (const struct sockaddr*)&bind_addr, 0);
	if (result != 0) {
		std::cerr << "Failed to bind UDP socket: " << uv_strerror(result) << std::endl;
		uv_close((uv_handle_t*)&udp_socket_, NULL);
		return result;
	}

	int send_buffer_size = 16 * 1024 * 1024;
	int recv_buffer_size = 16 * 1024 * 1024;
	uv_send_buffer_size((uv_handle_t*)&udp_socket_, &send_buffer_size);
	uv_recv_buffer_size((uv_handle_t*)&udp_socket_, &recv_buffer_size);

	result = uv_timer_init(loop_, &timer_);
	if (result != 0) {
		std::cerr << "Failed to init timer: " << uv_strerror(result) << std::endl;
		uv_close((uv_handle_t*)&udp_socket_, NULL);
		return result;
	}
	timer_.data = this;

	init_rttp_engine();

	run_ = true;

	uv_timer_start(&timer_, on_timer, 5, 10);

	if (http_monitor_) {
		http_monitor_->start();
	}

	uv_udp_recv_start(&udp_socket_, alloc_udp_buffer, on_udp_recv);

	return 0;
}


void rttp_server::stop()
{
	if (!run_)
		return;

	run_ = false;

	uv_timer_stop(&timer_);
	uv_close((uv_handle_t*)&timer_, NULL);

	uv_udp_recv_stop(&udp_socket_);
	uv_close((uv_handle_t*)&udp_socket_, NULL);

	if (http_monitor_) {
		http_monitor_->stop();
	}

	uninit_rttp_engine();
}
