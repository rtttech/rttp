#pragma once

#include <vector>
#include <chrono>
#include <time.h>

#include "rtsocket.h"
#include "stats_manager.h"
#include "http_monitor.h"

class rttp_server;

struct bridge_param
{
	std::vector<std::pair<std::string, int>> upstream_address;
	int max_connections = 1000;
};


class rttp_tcp_bridge
{
public:
	rttp_tcp_bridge(RTEngine engine, RTSOCKET downstream_socket, uv_loop_t* loop, const bridge_param& param, rttp_server* server, stats_manager* stats_mgr, http_monitor* http_monitor);
	~rttp_tcp_bridge();

	void on_downstream_connected();

	void on_downstream_readable();
	void on_downstream_writable();
	void on_downstream_error();

private:
	void do_rttp_recv();
	void do_tcp_recv();

	void do_rttp_send();
	void do_tcp_send();

	void close();

	// Try to flush stats if enough time has passed (3 seconds)
	void try_flush_stats();

	// libuv callbacks
	static void on_resolved(uv_getaddrinfo_t* req, int status, struct addrinfo* res);
	static void on_tcp_connect(uv_connect_t* req, int status);
	static void on_tcp_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf);
	static void on_tcp_write(uv_write_t* req, int status);
	static void on_tcp_handle_closed(uv_handle_t* handle);
	static void alloc_buffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);

private:

	uv_loop_t* loop_;
	

	RTEngine rttp_engine_;
	RTSOCKET downstream_rttp_socket_;       // Downstream RTTP connection
	uv_tcp_t upstream_tcp_socket_;          // Upstream TCP connection
	uv_getaddrinfo_t resolver_req_;         // DNS resolution request
	uv_write_t write_req_;                  // Reused write request

	// Data buffers
	enum { max_length = 32768 };
	enum { accumulated_buffer_size = 32768 };  // 32KB accumulated buffer
	char tcp_buffer_[max_length];           
	char accumulated_buffer_[accumulated_buffer_size];
	int accumulated_data_size_ = 0;
	bool tcp_reading_ = false;
	bool tcp_writting_ = false;
	int tcp_data_size_ = 0;
	int tcp_data_pos_ = 0;

	// Status flags
	bool upstream_connected_ = false;       // Upstream connection status
	bool tcp_handle_initialized_ = false;   // Whether TCP handle is initialized
	bool tcp_connecting_ = false;           // Whether TCP connection is in progress
	int pending_close_handles_ = 0;         // Number of handles waiting to be closed

	const bridge_param& param_;
	rttp_server* server_;
	stats_manager* stats_mgr_;
	http_monitor* http_monitor_;
	std::string connection_id_;

	// Local stats accumulation (to reduce cross-thread calls)
	uint64_t local_bytes_sent_ = 0;
	uint64_t local_bytes_received_ = 0;
	time_t last_stats_flush_time_;

	bool closed_ = false;
	
	static uint64_t next_connection_id_;
};