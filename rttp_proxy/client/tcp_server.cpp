#include "tcp_server.h"
#include <iostream>
#include <cstring>

// UDP send request structure
struct udp_send_req_t {
	uv_udp_send_t req;
	uv_buf_t buf;
};

tcp_server::tcp_server(uv_loop_t* loop,
	const std::string& local_host, unsigned short local_port,
	const bridge_param& param)
	:
	loop_(loop),
	rttp_engine_(NULL),
	param_(param),
	local_host_(local_host),
	local_port_(local_port)
{
}

tcp_server::~tcp_server()
{
	stop();
}

int tcp_server::start()
{
	if (run_)
		return 0;

    // Initialize TCP server handle
    int result = uv_tcp_init(loop_, &tcp_server_);
	if (result != 0) {
		std::cerr << "Failed to init TCP socket: " << uv_strerror(result) << std::endl;
		return result;
	}
    tcp_server_.data = this;

    struct sockaddr_in addr;
    uv_ip4_addr(local_host_.c_str(), local_port_, &addr);
    result = uv_tcp_bind(&tcp_server_, (const struct sockaddr*)&addr, 0);
	if (result != 0) {
		std::cerr << "Failed to bind TCP socket: " << uv_strerror(result) << std::endl;
		uv_close((uv_handle_t*)&tcp_server_, NULL);
		return result;
	}

    // Initialize UDP socket
    result = uv_udp_init(loop_, &udp_socket_);
	if (result != 0) {
		std::cerr << "Failed to init UDP socket: " << uv_strerror(result) << std::endl;
		uv_close((uv_handle_t*)&tcp_server_, NULL);
		return result;
	}
    udp_socket_.data = this;

    // Bind UDP socket to any port (0)
    struct sockaddr_in udp_addr;
    uv_ip4_addr("0.0.0.0", 0, &udp_addr);
    result = uv_udp_bind(&udp_socket_, (const struct sockaddr*)&udp_addr, 0);
	if (result != 0) {
		std::cerr << "Failed to bind UDP socket: " << uv_strerror(result) << std::endl;
		uv_close((uv_handle_t*)&tcp_server_, NULL);
		uv_close((uv_handle_t*)&udp_socket_, NULL);
		return result;
	}

    // Set UDP buffer size
    int send_buffer_size = 16 * 1024 * 1024;
	int recv_buffer_size = 16 * 1024 * 1024;
	uv_send_buffer_size((uv_handle_t*)&udp_socket_, &send_buffer_size);
	uv_recv_buffer_size((uv_handle_t*)&udp_socket_, &recv_buffer_size);

    // Initialize timer
    result = uv_timer_init(loop_, &timer_);
	if (result != 0) {
		std::cerr << "Failed to init timer: " << uv_strerror(result) << std::endl;
		uv_close((uv_handle_t*)&tcp_server_, NULL);
		uv_close((uv_handle_t*)&udp_socket_, NULL);
		return result;
	}
    timer_.data = this;

	init_rttp_engine();

	run_ = true;

    // Start listening for TCP connections
    result = uv_listen((uv_stream_t*)&tcp_server_, 128, on_connection_cb);
    if (result != 0) {
        std::cerr << "Listen error: " << uv_strerror(result) << std::endl;
		stop();
		return result;
    }

	do_udp_receive();

    uv_timer_start(&timer_, on_timer, 5, 10);

	return 0;
}

void tcp_server::stop()
{
	if (!run_)
		return;

	run_ = false;
	
    uv_timer_stop(&timer_);
    uv_close((uv_handle_t*)&timer_, NULL);
    
    if (!uv_is_closing((uv_handle_t*)&udp_socket_))
        uv_close((uv_handle_t*)&udp_socket_, NULL);
    
    if (!uv_is_closing((uv_handle_t*)&tcp_server_))
        uv_close((uv_handle_t*)&tcp_server_, NULL);

	uninit_rttp_engine();
}


void tcp_server::init_rttp_engine()
{
	if (rttp_engine_ == NULL) {
		rttp_engine_ = rt_init("");
		rt_set_callback(rttp_engine_, on_rtsocket_event, packet_send_imp);
	}
}

void tcp_server::uninit_rttp_engine()
{
	if (rttp_engine_ != NULL) {
		rt_uninit(rttp_engine_);
		rttp_engine_ = NULL;
	}
}

void tcp_server::on_new_connection(uv_stream_t* server, int status)
{
    if (status < 0) {
        std::cerr << "New connection error: " << uv_strerror(status) << std::endl;
        return;
    }

	std::cout << "incoming tcp connection" << std::endl;

	if (cur_connections_num_ < param_.max_connections) {
        uv_tcp_t* client = (uv_tcp_t*)malloc(sizeof(uv_tcp_t));
        uv_tcp_init(loop_, client);

        if (uv_accept(server, (uv_stream_t*)client) == 0) {
            // Disable Nagle's algorithm
            uv_tcp_nodelay(client, 1);

            auto rttp_socket = rt_socket(rttp_engine_, 0);
            rt_set_userdata(rttp_engine_, rttp_socket, 0, this);

            tcp_rttp_bridge* bridge_ptr = new tcp_rttp_bridge(rttp_engine_, rttp_socket, client, loop_, param_, this);
            rt_set_userdata(rttp_engine_, rttp_socket, 1, bridge_ptr);
            cur_connections_num_++;

            bridge_ptr->on_downstream_connected();
        }
        else {
            uv_close((uv_handle_t*)client, [](uv_handle_t* handle){
                free(handle);
            });
        }
	}
	else {
        uv_tcp_t* client = (uv_tcp_t*)malloc(sizeof(uv_tcp_t));
        uv_tcp_init(loop_, client);
        if (uv_accept(server, (uv_stream_t*)client) == 0) {
            uv_close((uv_handle_t*)client, [](uv_handle_t* handle){
                free(handle);
            });
        } else {
             uv_close((uv_handle_t*)client, [](uv_handle_t* handle){
                free(handle);
            });
        }
	}
}

void tcp_server::handle_timer()
{
	rt_tick(rttp_engine_);
}


void tcp_server::do_udp_receive()
{
    uv_udp_recv_start(&udp_socket_, alloc_buffer, on_udp_recv);
}

void tcp_server::handle_udp_packet(const char* data, ssize_t nread, const struct sockaddr* addr)
{
    RTSOCKET socket = rt_incoming_packet(rttp_engine_, data, (int)nread, addr, sizeof(struct sockaddr_storage));
    if (socket != NULL) {
        std::cout << "unexpected incoming rttp connection" << std::endl;
        rt_close(rttp_engine_, socket);
    }
}


void tcp_server::on_rtsocket_write(RTEngine engine, RTSOCKET socket)
{
	tcp_rttp_bridge* bridge_ptr = (tcp_rttp_bridge*)rt_get_userdata(engine, socket, 1);
    if (bridge_ptr)
	    bridge_ptr->on_upstream_writable();
}

void tcp_server::on_rtsocket_read(RTEngine engine, RTSOCKET socket)
{
	tcp_rttp_bridge* bridge_ptr = (tcp_rttp_bridge*)rt_get_userdata(engine, socket, 1);
    if (bridge_ptr)
	    bridge_ptr->on_upstream_readable();
}

void tcp_server::on_rtsocket_connect(RTEngine engine, RTSOCKET socket)
{
	tcp_rttp_bridge* bridge_ptr = (tcp_rttp_bridge*)rt_get_userdata(engine, socket, 1);
    if (bridge_ptr)
	    bridge_ptr->on_upstream_connected();
}

void tcp_server::on_rtsocket_error(RTEngine engine, RTSOCKET socket)
{
	tcp_rttp_bridge* bridge_ptr = (tcp_rttp_bridge*)rt_get_userdata(engine, socket, 1);
    if (bridge_ptr)
	    bridge_ptr->on_upstream_error();
}

void tcp_server::on_rtsocket_event(RTEngine engine, RTSOCKET socket, int event)
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

void tcp_server::packet_send_imp(RTEngine engine, RTSOCKET socket, const char* data, int len, const struct sockaddr* sa, int sock_len)
{
	tcp_server* server_ptr = (tcp_server*)rt_get_userdata(engine, socket, 0);

    uv_buf_t buf;
    buf.base = const_cast<char*>(data);
    buf.len = len;

    int result = uv_udp_try_send(&server_ptr->udp_socket_, &buf, 1, sa);
    
    if (result < 0) {
        std::cout << "uv_udp_try_send return " << result << std::endl;
    }
}

// libuv callbacks
void tcp_server::alloc_buffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf)
{
    static char recv_buff[65536];
    buf->base = recv_buff;
    buf->len = sizeof(recv_buff);
}

void tcp_server::on_udp_recv(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf, const struct sockaddr* addr, unsigned flags)
{
    tcp_server* server = (tcp_server*)handle->data;

    if (nread > 0 && addr != NULL) {
        server->handle_udp_packet(buf->base, nread, addr);
    }
    else if (nread < 0) {
        std::cerr << "UDP receive error: " << uv_strerror(nread) << std::endl;
    }
}

void tcp_server::on_timer(uv_timer_t* handle)
{
    tcp_server* server = (tcp_server*)handle->data;
    server->handle_timer();
}

void tcp_server::on_connection_cb(uv_stream_t* server, int status)
{
    tcp_server* server_ptr = (tcp_server*)server->data;
    server_ptr->on_new_connection(server, status);
}

void tcp_server::on_udp_send(uv_udp_send_t* req, int status)
{
    udp_send_req_t* send_req = (udp_send_req_t*)req;
    free(send_req->buf.base);
    free(send_req);
}