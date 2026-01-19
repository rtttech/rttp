#pragma once

#include <uv.h>
#include <vector>
#include <memory>

#include "rtsocket.h"
#include "rttp_tcp_bridge.h"
#include "stats_manager.h"
#include "http_monitor.h"


class rttp_server
{
public:
	rttp_server(uv_loop_t* loop, const std::string local_host, short local_port, const bridge_param& param, short http_port);
	~rttp_server();

	int start();
	void stop();

	void update_param(const bridge_param& param) { param_ = param; }
	void on_connection_closed() { cur_connections_num_--; }
	
	// Health check support
	bool is_ready() const { return run_; }
	uint64_t get_uptime_seconds() const;

private:
	void init_rttp_engine();
	void uninit_rttp_engine();

	void do_udp_receive();
	
	void handle_timer();
	void handle_udp_packet(const char* data, ssize_t nread, const struct sockaddr* addr);
	void on_incoming_rttp_connection(RTSOCKET socket);

private:
	static void on_rtsocket_write(RTEngine engine, RTSOCKET socket);
	static void on_rtsocket_read(RTEngine engine, RTSOCKET socket);
	static void on_rtsocket_connect(RTEngine engine, RTSOCKET socket);
	static void on_rtsocket_error(RTEngine engine, RTSOCKET socket);
	static void on_rtsocket_event(RTEngine engine, RTSOCKET socket, int event);
	
	static void packet_send_imp(RTEngine engine, RTSOCKET socket, const char* data, int len, const struct sockaddr* sa, int sock_len);

	// libuv callbacks
	static void on_udp_recv(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf, const struct sockaddr* addr, unsigned flags);
	static void alloc_udp_buffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);
	static void on_timer(uv_timer_t* handle);
	static void on_udp_send(uv_udp_send_t* req, int status);
		
private:
	uv_loop_t* loop_;
	uv_udp_t udp_socket_;
	uv_timer_t timer_;

	RTEngine rttp_engine_;
	int max_connections_;
	int cur_connections_num_;

	enum { max_length = 2048 };
	char data_[max_length];

	bool run_ = false;
	char state[4096] = { 0 };

	bridge_param param_;
	
	// HTTP monitor (manages its own thread and libuv loop)
	stats_manager stats_mgr_;
	std::shared_ptr<http_monitor> http_monitor_;
	
	// Server status
	std::chrono::steady_clock::time_point start_time_;

	std::string local_host_;
	short local_port_;
	short http_port_;
};
