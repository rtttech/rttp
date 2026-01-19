#include "rttp_async_echo_client.h"
#include <chrono>



void init_socket()
{
#ifdef _WIN32
	WSAData wsaData;
	int init_ret = ::WSAStartup(MAKEWORD(2, 2), &wsaData);

#endif
}

SOCKET create_udp_socket(int buffer_size = 1024*1024)
{
#ifdef _WIN32
	
	SOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	u_long arg = 1;
	int ret = ::ioctlsocket(s, FIONBIO, &arg);

#else
	SOCKET s = ::socket(AF_INET, SOCK_DGRAM, 0);
	int oldflags = ::fcntl(s, F_GETFL, 0);
	oldflags |= O_NONBLOCK;
	::fcntl(s, F_SETFL, oldflags);
#endif // _MSC_VER


	int bf = buffer_size;
	int ssrr = setsockopt(s, SOL_SOCKET, SO_RCVBUF, (const char*)&bf, (int)sizeof(bf));
	int sssr = setsockopt(s, SOL_SOCKET, SO_SNDBUF, (const char*)&bf, (int)sizeof(bf));
	if (ssrr != 0 && sssr != 0) {
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
			if (item.send_buff_pos == item.send_buff_len) {
				conn.send_deq.pop_front();
			}
		}
	}
}

void on_rtsocket_connect(RTEngine engine, RTSOCKET rttp_socket)
{
	//cout << "\nsocket " << socket << " connected" << endl;

	rtsocket_client_context *ctx_ptr = (rtsocket_client_context*)rt_get_userdata(engine, rttp_socket, 0);

	auto rttp_conn_ptr = new rttp_connection();
	rt_set_userdata(engine, rttp_socket, 1, rttp_conn_ptr);

	std::shared_ptr<rttp_connection> ptr(rttp_conn_ptr);

	ctx_ptr->socket_map[rttp_socket] = ptr;
}


void on_rtsocket_read(RTEngine engine, RTSOCKET rttp_socket)
{
	rtsocket_client_context *ctx_ptr = (rtsocket_client_context*)rt_get_userdata(engine, rttp_socket, 0);
	void* userdata = rt_get_userdata(engine, rttp_socket, 1);
	if (userdata == NULL) return;
	rttp_connection& conn = *(rttp_connection*)userdata;
	//rttp_connection &si = *ctx_ptr->socket_map[rttp_socket];

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
				ctx_ptr->socket_map.erase(rttp_socket);
				return;
			}
			else if (recv_bytes > 0) {
				conn.recv_buff_pos += recv_bytes;
			}
			else {
				return;
			}
		}

		int packet_size = *((uint32_t*)conn.recv_buffer);
		if (packet_size < 4 || packet_size > 4096) {
			std::cout << "rttp invalid packet size: " << packet_size << std::endl;
			ctx_ptr->run = false;
			return;
		}

		while (conn.recv_buff_pos < packet_size) {
			int recv_bytes = rt_recv(engine, rttp_socket, conn.recv_buffer + conn.recv_buff_pos, packet_size - conn.recv_buff_pos, 0);
			if (recv_bytes == 0) {
				rt_close(engine, rttp_socket);
				cout << "socket " << rttp_socket << " closed" << endl;
				rt_set_userdata(engine, rttp_socket, 1, NULL);
				ctx_ptr->socket_map.erase(rttp_socket);
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
			//cout << "received " << packet_size << " bytes" << endl;
			uint32_t packet_seq = *((uint32_t*)(conn.recv_buffer + 4));

			sent_packet_info &spi = ctx_ptr->sent_packet_info_map[packet_seq];
			spi.resp_time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
			conn.recv_buff_pos = 0;
			ctx_ptr->latency_deq.push_back(spi.resp_time - spi.send_time);
			ctx_ptr->sent_packet_info_map.erase(packet_seq);

			if (ctx_ptr->latency_deq.size() > 10) {
				ctx_ptr->latency_deq.pop_front();
			}

			ctx_ptr->avg_latency = std::accumulate(ctx_ptr->latency_deq.begin(), ctx_ptr->latency_deq.end(), 0) / ctx_ptr->latency_deq.size();
		}
	}
}


void on_rtsocket_write(RTEngine engine, RTSOCKET rttp_socket)
{
	rtsocket_client_context *ctx_ptr = (rtsocket_client_context*)rt_get_userdata(engine, rttp_socket, 0);
	void* userdata = rt_get_userdata(engine, rttp_socket, 1);
	if (userdata == NULL) return;
	rttp_connection& conn = *(rttp_connection*)userdata;
	rtsocket_send_data(engine, rttp_socket, conn);
}



void on_rtsocket_error(RTEngine engine, RTSOCKET rttp_socket)
{
	rtsocket_client_context *ctx_ptr = (rtsocket_client_context*)rt_get_userdata(engine, rttp_socket, 0);

	int errcode = rt_get_error(engine, rttp_socket);

	cout << "\nrttp socket " << rttp_socket << " error: " << errcode << endl;
	rt_close(engine, rttp_socket);
	rt_set_userdata(engine, rttp_socket, 1, NULL);
	ctx_ptr->socket_map.erase(rttp_socket);
}

void on_rtsocket_event(RTEngine engine, RTSOCKET rttp_socket, int event)
{
	switch (event){
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

void packet_send_imp(RTEngine engine, RTSOCKET rttp_socket, const char * data, int len,  const struct sockaddr *sa, int sock_len)
{
	rtsocket_client_context *ctx_ptr = (rtsocket_client_context*)rt_get_userdata(engine, rttp_socket, 0);

	int64_t send_start = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
	int send_ret = ::sendto(ctx_ptr->socket, data, len, 0, (struct sockaddr *)sa, sock_len);
	int64_t send_end = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
	//cout << "udp send take " << send_end - send_start << endl;
	//cout << "send return " << send_ret;
}

void send_ping_packet(RTEngine engine, RTSOCKET rttp_socket, int packet_num )
{
	rtsocket_client_context *ctx_ptr = (rtsocket_client_context*)rt_get_userdata(engine, rttp_socket, 0);

	rttp_connection& conn = *(rttp_connection*)rt_get_userdata(engine, rttp_socket, 1);

	if (conn.send_deq.size() > 5 ||!rt_connected(engine, rttp_socket))
		return;

	for (int i = 0; i < packet_num; ++i) {
		int packet_size[] = { 100, 300, 500, 800, 1100 };
		int rand_bytes = packet_size[rand() % (sizeof(packet_size) / sizeof(int))];
		int int_num = rand_bytes / 4;
		uint32_t *buffer = new uint32_t[int_num];
		buffer[0] = rand_bytes;

		static uint32_t s_packet_seq = 0;

		buffer[1] = ++s_packet_seq;

		for (int i = 2; i < int_num; ++i) {
			buffer[i] = rand();
		}
       

		//cout << "send " << rand_bytes << " return " << send_ret << endl;
		sent_packet_info spi;
		spi.pkt_size = rand_bytes;
		spi.send_time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
		ctx_ptr->sent_packet_info_map[s_packet_seq] = spi;


		int send_bytes = 0;

		if (conn.send_deq.size() == 0) {
			while (send_bytes < rand_bytes) {
				int send_ret = rt_send(engine, rttp_socket, (const char*)buffer + send_bytes, rand_bytes - send_bytes, 0);
				if (send_ret > 0) {
					send_bytes += send_ret;
				}
				else {
					break;
				}
			}
		}

		if (send_bytes == rand_bytes) {
			delete[] buffer;
		}
		else {
			std::shared_ptr<socket_send_item> ptr(new socket_send_item((const char*)buffer, rand_bytes, send_bytes));
			conn.send_deq.push_back(ptr);
		}
	}
}

void send_ping(rtsocket_client_context* ctx_ptr, uint64_t interval = 500*1000, int packet_num = 3)
{
	static uint64_t last_send_ping = 0;

	auto cur_time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
	if (cur_time - last_send_ping > interval) {
		last_send_ping = cur_time;
		std::unordered_map<RTSOCKET, std::shared_ptr<rttp_connection>>::iterator iter;
		for (iter = ctx_ptr->socket_map.begin(); iter != ctx_ptr->socket_map.end(); ++iter) {
			RTSOCKET rts = iter->first;
			send_ping_packet(ctx_ptr->rttp_engine, rts, packet_num);
		}
	}
}



void do_udp_recv(rtsocket_client_context* ctx_ptr, SOCKET socket)
{
	char buff[2000] = { 0 };

	int i = 0;
	while (i++ < 1000) {
		struct sockaddr_in sa;
		socklen_t from_len = sizeof(sa);
		memset(&sa, 0, from_len);

		//cout << "start recv: ";
		//int64_t recv_start = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
		int bytes = ::recvfrom(socket, buff, sizeof(buff), 0, (struct sockaddr*)&sa, &from_len);
		//int64_t recv_end = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();

		//cout << " recv take " << recv_end - recv_start << endl;

		//cout << "udp recv return " << bytes << endl;
		if (bytes > 0) {
			++ctx_ptr->total_received_udp_pkt;
			//cout << "total received: " << total_received_udp_pkt << endl;
			int64_t handle_start = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
			//if (rand() % 3 != 0) //packet lost test
			{
				RTSOCKET s = rt_incoming_packet(ctx_ptr->rttp_engine, buff, bytes, (const struct sockaddr *)&sa, from_len);
				if (s != NULL) {
					rt_set_userdata(ctx_ptr->rttp_engine, s, 0, ctx_ptr);
				}
				int64_t handle_end = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
				//cout << bytes << " bytes "<<" handle packet take " << handle_end - handle_start << endl;
			}
		}
		else {
			return;
		}
	}
}


int rttp_client_main_func(rtsocket_client_context* ctx_ptr, const char* remote_ip, int port, int interval, int packet_num)
{
	init_socket();

	ctx_ptr->socket = create_udp_socket();

	struct sockaddr_in sa;
	memset(&sa, 0, sizeof(sa));

#if defined(SOCKADDR_WITH_LEN)
	sa.sin_len = sizeof(sa);
#endif
    
	sa.sin_family = AF_INET;
	
	sa.sin_addr.s_addr = inet_addr(remote_ip);
	sa.sin_port = htons(port);

	if (sa.sin_addr.s_addr == -1) {
		ctx_ptr->run = false;
		return 0;
	}


	uint64_t last_periodical_time = 0;
	uint64_t last_print_latency = 0;

	cout << "rttp version: " << rt_get_version() << endl;

	ctx_ptr->rttp_engine = rt_init("");
	rt_set_callback(ctx_ptr->rttp_engine, on_rtsocket_event, packet_send_imp);

	RTSOCKET client_rtsocket;
	client_rtsocket = rt_socket(ctx_ptr->rttp_engine, RTSM_LOW_LATENCY);
	
	rt_set_userdata(ctx_ptr->rttp_engine, client_rtsocket, 0, ctx_ptr);
	rt_connect(ctx_ptr->rttp_engine, client_rtsocket, (const struct sockaddr*)&sa, sizeof(sa));

	while (ctx_ptr->run) {

		fd_set rfds;
		FD_ZERO(&rfds);
		FD_SET(ctx_ptr->socket, &rfds);
		fd_set* read_fs_ptr = &rfds;

		fd_set* write_fs_ptr = NULL;

		int waitms = 5;
		struct timeval tv;
		tv.tv_sec = 0;
		tv.tv_usec = waitms * 1000;
		int num = ::select(ctx_ptr->socket + 1, read_fs_ptr, write_fs_ptr, NULL, &tv);
		if (num > 0) {
			if (FD_ISSET(ctx_ptr->socket, read_fs_ptr)) {
				do_udp_recv(ctx_ptr, ctx_ptr->socket);
			}
		}

		uint64_t cur_time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
		if (cur_time - last_periodical_time >= 10000) {
			last_periodical_time = cur_time;
			rt_tick(ctx_ptr->rttp_engine);
			send_ping(ctx_ptr, interval *1000, packet_num);
			int bytes = rt_state_desc(ctx_ptr->rttp_engine, client_rtsocket, ctx_ptr->state, sizeof(ctx_ptr->state));
			if (bytes > 0 && bytes < sizeof(ctx_ptr->state)) {
				ctx_ptr->state[bytes] = 0;
			}
		}

	}

	std::unordered_map<RTSOCKET, std::shared_ptr<rttp_connection>>::iterator iter;
	for (iter = ctx_ptr->socket_map.begin(); iter != ctx_ptr->socket_map.end(); ++iter) {
		rt_close(ctx_ptr->rttp_engine, iter->first);
	}
	ctx_ptr->socket_map.clear();
	ctx_ptr->latency_deq.clear();
	ctx_ptr->sent_packet_info_map.clear();
	

	close_socket(ctx_ptr->socket);
	ctx_ptr->socket = -1;

	rt_uninit(ctx_ptr->rttp_engine);
	cout << "rttp exit" << endl;

	return 0;
}