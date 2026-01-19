#include "rtsocket.h"
#include "rttp_async_echo_server.h"

#include <iostream>
#include <deque>
#include <set>
#include <map>
#include <assert.h>
#include <string.h>
#include <memory>
#include <thread>
#include <unordered_map>
#include <condition_variable>

#include <chrono>

using namespace std;


void init_socket()
{
#ifdef _WIN32
	WSAData wsaData;
	int init_ret = ::WSAStartup(MAKEWORD(2, 2), &wsaData);

#endif
}

SOCKET create_udp_socket(int buffer_size = 1024 * 1024)
{
#ifdef _WIN32

	SOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	u_long arg = 1;
	int ret = ioctlsocket(s, FIONBIO, &arg);
	if (ret != 0) {
		std::cout << "ioctlsocket failed: " << WSAGetLastError() << std::endl;
	}

#else
	SOCKET s = ::socket(AF_INET, SOCK_DGRAM, 0);
	int oldflags = ::fcntl(s, F_GETFL, 0);
	oldflags |= O_NONBLOCK;
	::fcntl(s, F_SETFL, oldflags);
#endif // _WIN32


	int bf = buffer_size;
	int ssrr = setsockopt(s, SOL_SOCKET, SO_RCVBUF, (const char*)&bf, (int)sizeof(bf));
	int sssr = setsockopt(s, SOL_SOCKET, SO_SNDBUF, (const char*)&bf, (int)sizeof(bf));
	if (ssrr != 0 || sssr != 0) {
		std::cout << "setsockopt failed" << std::endl;
	}
	else {
		//std::cout << "setsockopt success" << std::endl;
	}

	return s;
}

void close_socket(SOCKET s)
{
#ifdef _WIN32
	::closesocket(s);
#else
	::close(s);
#endif
}

void rtsocket_send_data(RTEngine engine, RTSOCKET rttp_socket, rttp_connection& conn)
{
	while (conn.send_deq.size() > 0) {
		socket_send_item& item = *conn.send_deq[0];

		int send_bytes = rt_send(engine, rttp_socket, item.send_buffer + item.send_buff_pos, item.send_buff_len - item.send_buff_pos, 0);
		if (send_bytes <= 0) {
			return;
		}
		else {
			item.send_buff_pos += send_bytes;
			//cout << "send " << send_bytes << " bytes, total send "<<item.send_buff_pos << endl;
			if (item.send_buff_pos == item.send_buff_len) {
				conn.send_deq.pop_front();
			}
		}
	}
}


void on_rtsocket_connect(RTEngine engine, RTSOCKET rttp_socket)
{
	
}

void on_rtsocket_read(RTEngine engine, RTSOCKET rttp_socket)
{
	rtsocket_server_context *ctx_ptr = (rtsocket_server_context *)rt_get_userdata(engine, rttp_socket, 0);
	void* userdata = rt_get_userdata(engine, rttp_socket, 1);
	if (userdata == NULL) return;
	rttp_connection &conn = *(rttp_connection *)userdata;

	while (true) {
		if (conn.recv_buffer == NULL) {
			conn.recv_buff_len = 4096;
			conn.recv_buff_pos = 0;
			conn.recv_buffer = new char[conn.recv_buff_len];
		}

		while (conn.recv_buff_pos < 4) {
			int recv_bytes = rt_recv(engine, rttp_socket, conn.recv_buffer + conn.recv_buff_pos, 4 - conn.recv_buff_pos, 0);
			if (recv_bytes == 0) {
				rt_close(engine, rttp_socket);
				rt_set_userdata(engine, rttp_socket, 1, NULL);
				ctx_ptr->connected_socket_map.erase(rttp_socket);
				return;
			}
			else if (recv_bytes > 0) {
				conn.recv_buff_pos += recv_bytes;
			}
			else {
				return;
			}
		}

		uint32_t packet_size = 0;
		memcpy(&packet_size, conn.recv_buffer, sizeof(packet_size));

		if (packet_size < 4 || packet_size > 3000) {
			cout << "received invalid packet size from rttp socket " << rttp_socket << endl;
			rt_close(engine, rttp_socket);
			rt_set_userdata(engine, rttp_socket, 1, NULL);
			ctx_ptr->connected_socket_map.erase(rttp_socket);
			return;
		}

		while (conn.recv_buff_pos < packet_size) {
			int recv_bytes = rt_recv(engine, rttp_socket, conn.recv_buffer + conn.recv_buff_pos, packet_size - conn.recv_buff_pos, 0);
			if (recv_bytes == 0) {
				rt_close(engine, rttp_socket);
				rt_set_userdata(engine, rttp_socket, 1, NULL);
				ctx_ptr->connected_socket_map.erase(rttp_socket);
				return;
			}
			else if (recv_bytes > 0) {
				conn.recv_buff_pos += recv_bytes;
			}
			else {
				return;
			}
		}

		if (conn.recv_buff_pos == packet_size) {
            //cout<<"received "<<packet_size<<" bytes"<<endl;
			conn.send_deq.push_back(std::shared_ptr<socket_send_item>(new socket_send_item(conn.recv_buffer, packet_size)));
			conn.recv_buffer = NULL;
			conn.recv_buff_len = 0;
			conn.recv_buff_pos = 0;

			rtsocket_send_data(engine, rttp_socket, conn);
		}
	}
}

void on_rtsocket_write(RTEngine engine, RTSOCKET rttp_socket)
{
	rtsocket_server_context *ctx_ptr = (rtsocket_server_context *)rt_get_userdata(engine, rttp_socket, 0);
	void* userdata = rt_get_userdata(engine, rttp_socket, 1);
	if (userdata == NULL) return;
	rttp_connection &conn = *(rttp_connection *)userdata;
	
	rtsocket_send_data(engine, rttp_socket, conn);
}

void on_rtsocket_error(RTEngine engine, RTSOCKET rttp_socket)
{
	rtsocket_server_context *ctx_ptr = (rtsocket_server_context *)rt_get_userdata(engine, rttp_socket, 0);

	int errcode = rt_get_error(engine, rttp_socket);
    cout<<"rttp socket "<< rttp_socket << " error " << errcode <<endl;
	rt_close(engine, rttp_socket);

	rt_set_userdata(engine, rttp_socket, 1, NULL);
	ctx_ptr->connected_socket_map.erase(rttp_socket);
}

void on_rtsocket_event(RTEngine engine, RTSOCKET rttp_socket, int event)
{
	switch (event) {
	case RTTP_EVENT_CONNECT:
		on_rtsocket_connect(engine, rttp_socket);
		return;
	case RTTP_EVENT_READ:
		on_rtsocket_read(engine, rttp_socket);
		return;
	case RTTP_EVENT_WRITE:
		on_rtsocket_write(engine, rttp_socket);
		return;
	case RTTP_EVENT_ERROR:
		on_rtsocket_error(engine, rttp_socket);
		return;
	}
}

void packet_send_imp(RTEngine engine, RTSOCKET rttp_socket, const char * data, int len, const struct sockaddr* sa, int sock_len)
{
	rtsocket_server_context *ctx_ptr = (rtsocket_server_context *)rt_get_userdata(engine, rttp_socket, 0);

	//int64_t send_start = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
	 
	int ret = ::sendto(ctx_ptr->udp_socket, data, len, 0, (struct sockaddr *)sa, sock_len);
	//int64_t send_end = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
	//cout << "udp send take " << send_end - send_start << endl;

	if (ret < 0) {
		cout << "udp send return " << ret << std::endl;
	}
	
	++ctx_ptr->total_send;
	
}


void do_udp_recv(rtsocket_server_context *ctx_ptr, SOCKET udp_socket)
{
	char buff[2000];

	int i = 0;
	while (i++ < 100) {
		struct sockaddr_storage sa;
		socklen_t from_len = sizeof(sa);
		int bytes = ::recvfrom(udp_socket, buff, sizeof(buff), 0, (struct sockaddr*)&sa, &from_len);

		if (bytes > 0) {
			RTSOCKET s = rt_incoming_packet(ctx_ptr->engine, buff, bytes, (const struct sockaddr*)&sa, from_len);
			if (s != NULL && ctx_ptr->connected_socket_map.find(s) == ctx_ptr->connected_socket_map.end()) {
				cout << "incoming socket " << s << endl;
				std::unique_ptr<rttp_connection> ptr(new rttp_connection());
				rt_set_userdata(ctx_ptr->engine, s, 0, ctx_ptr);
				rt_set_userdata(ctx_ptr->engine, s, 1, ptr.get());
				ctx_ptr->connected_socket_map[s] = std::move(ptr);
				int mode = RTSM_LOW_LATENCY;
				rt_setsockopt(ctx_ptr->engine, s, RTSO_MODE, (char*)&mode, sizeof(mode));
				rt_accept(ctx_ptr->engine, s);
				
			}
		}
		else {
			return;
		}
	}
}


int server_main_func(rtsocket_server_context* ctx_ptr, int port)
{
	init_socket();
	ctx_ptr->udp_socket = create_udp_socket(16*1024*1024);
	
	struct sockaddr_in sa;
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	
	sa.sin_port = htons(port);

	int ret = ::bind(ctx_ptr->udp_socket, (struct sockaddr*)&sa, sizeof(sa));
	if (ret == 0) {
		cout << "listen on port " << port << endl;
	}
	else {
		cout << "bind port " << port << " failed" << endl;
		return 0;
	}

	
	uint64_t last_notify_tick_time = 0;
	uint64_t last_statistic_time = 0;

	cout << "rttp version: " << rt_get_version() << endl;

	ctx_ptr->engine = rt_init("");
	rt_set_callback(ctx_ptr->engine, on_rtsocket_event, packet_send_imp);

	while (true) {

		fd_set rfds;
		FD_ZERO(&rfds);
		FD_SET(ctx_ptr->udp_socket, &rfds);
		fd_set* read_fs_ptr = &rfds;

		fd_set* write_fs_ptr = NULL;

		int waitms = 1;
		struct timeval tv;
		tv.tv_sec = 0;
		tv.tv_usec = waitms * 1000;
		int num = ::select(ctx_ptr->udp_socket + 1, read_fs_ptr, write_fs_ptr, NULL, &tv);
		if (num > 0) {
			if (FD_ISSET(ctx_ptr->udp_socket, read_fs_ptr)) {
				do_udp_recv(ctx_ptr, ctx_ptr->udp_socket);
			}
		}
		
		uint64_t cur_time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
		if (cur_time - last_notify_tick_time >= 5000) {
			last_notify_tick_time = cur_time;
			//cout << "rt_tick" << endl;
			rt_tick(ctx_ptr->engine);
			//cout << "rt_tick end" << endl;
		}

		if (cur_time - last_statistic_time >= 1000 * 1000) {
			last_statistic_time = cur_time;

			uint32_t total = 0;
			std::unordered_map<RTSOCKET, std::unique_ptr<rttp_connection>>::iterator iter;
			for (iter = ctx_ptr->connected_socket_map.begin(); iter != ctx_ptr->connected_socket_map.end(); ++iter) {
				total += iter->second->send_deq.size();
			}
			ctx_ptr->total_waiting_send_num = total;
			ctx_ptr->connection_num = ctx_ptr->connected_socket_map.size();
			if (ctx_ptr->connected_socket_map.size() >= 1) {
				int bytes = rt_state_desc(ctx_ptr->engine, ctx_ptr->connected_socket_map.begin()->first, ctx_ptr->state, sizeof(ctx_ptr->state));
				if (bytes > 0 && bytes < sizeof(ctx_ptr->state)) {
					ctx_ptr->state[bytes] = 0;
				}
			}
		}
	}

	close_socket(ctx_ptr->udp_socket);

	rt_uninit(ctx_ptr->engine);

	return 0;
}

