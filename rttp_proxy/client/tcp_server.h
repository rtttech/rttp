#pragma once

#include <uv.h>
#include <vector>
#include <string>
#include <memory>

#include "rtsocket.h"
#include "tcp_rttp_bridge.h"

struct bridge_param;

class tcp_server
{
public:

    tcp_server(uv_loop_t* loop,
        const std::string& local_host, unsigned short local_port,
        const bridge_param& param);

	~tcp_server();

	int start();
	void stop();

	void update_param(const bridge_param& param) { param_ = param; }
	void on_connection_closed() { cur_connections_num_--; }

private:
	void do_udp_receive();

	void init_rttp_engine();
	void uninit_rttp_engine();

	void handle_timer();
    void handle_udp_packet(const char* data, ssize_t nread, const struct sockaddr* addr);
    void on_new_connection(uv_stream_t* server, int status);
	
private:
	static void on_rtsocket_write(RTEngine engine, RTSOCKET socket);
	static void on_rtsocket_read(RTEngine engine, RTSOCKET socket);
	static void on_rtsocket_connect(RTEngine engine, RTSOCKET socket);
	static void on_rtsocket_error(RTEngine engine, RTSOCKET socket);
	static void on_rtsocket_event(RTEngine engine, RTSOCKET socket, int event);

	static void packet_send_imp(RTEngine engine, RTSOCKET socket, const char* data, int len, const struct sockaddr* sa, int sock_len);

    // libuv callbacks
	static void on_udp_recv(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf, const struct sockaddr* addr, unsigned flags);
	static void alloc_buffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);
	static void on_timer(uv_timer_t* handle);
    static void on_connection_cb(uv_stream_t* server, int status);
    static void on_udp_send(uv_udp_send_t* req, int status);

private:
	RTEngine rttp_engine_ = NULL;

    uv_loop_t* loop_;
    uv_tcp_t tcp_server_;
    uv_udp_t udp_socket_;
    uv_timer_t timer_;

	enum { max_length = 2048 };
	char data_[max_length];

	int cur_connections_num_ = 0;

	char state[4096] = { 0 };

	bridge_param param_;
    
	bool run_ = false;

	std::string local_host_;
	unsigned short local_port_;
};