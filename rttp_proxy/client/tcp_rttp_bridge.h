#pragma once

#include <uv.h>
#include <vector>
#include <string>

#include "rtsocket.h"

class tcp_server;

struct bridge_param
{
	std::vector<std::pair<std::string, int>> upstream_address;
	int max_connections = 1000;
};

class tcp_rttp_bridge
{
public:
	tcp_rttp_bridge(RTEngine engine, RTSOCKET upstream_socket, uv_tcp_t* downstream_socket, uv_loop_t* loop, const bridge_param &param, tcp_server* server);
    ~tcp_rttp_bridge();

public:
	void on_downstream_connected();
	void on_upstream_connected();
	void on_upstream_readable();
	void on_upstream_writable();
	void on_upstream_error();

private:
	// Read data from RTTP and forward to TCP
	void do_rttp_recv();

	// Read data from TCP and forward to RTTP
	void do_tcp_recv();

	void do_rttp_send();
	void do_tcp_send();


	// Close connection and clean up resources
	void close();

    // libuv callbacks
    static void on_resolved(uv_getaddrinfo_t* req, int status, struct addrinfo* res);
    static void alloc_buffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);
    static void on_tcp_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf);
    static void on_tcp_write(uv_write_t* req, int status);
    static void on_tcp_close(uv_handle_t* handle);

private:
    uv_loop_t* loop_;
	RTEngine rttp_engine_;
	RTSOCKET upstream_rttp_socket_;       // Upstream RTTP connection
	uv_tcp_t* downstream_tcp_socket_;  // 下游TCP连接 (owned by this class)

    uv_getaddrinfo_t resolver_req_;
    uv_write_t write_req_;

	// Data buffers
	enum { max_length = 32768};
	enum { accumulated_buffer_size = 32768 };  // 32KB accumulated buffer
	char tcp_buffer_[max_length];           // TCP receive buffer
	char accumulated_buffer_[accumulated_buffer_size];  // Accumulation buffer for batch writing
	int accumulated_data_size_ = 0;         // Data size in accumulation buffer
	bool tcp_reading_ = false;
	bool tcp_writting_ = false;
	int tcp_data_size_ = 0;
	int tcp_data_pos_ = 0;

	const bridge_param& param_;
	tcp_server* server_;

	bool closed_ = false;
    int pending_close_handles_ = 0;
};
