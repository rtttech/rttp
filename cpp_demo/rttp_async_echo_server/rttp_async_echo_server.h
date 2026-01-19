#include "rtsocket.h"

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



struct sent_packet_info
{
	sent_packet_info() : send_time(0), resp_time(0), pkt_size(0) {}
	uint64_t send_time;
	uint64_t resp_time;
	uint32_t pkt_size;
};

struct socket_send_item
{
	socket_send_item(const char* buffer, int len) : send_buffer(buffer), send_buff_len(len), send_buff_pos(0) {}
	~socket_send_item() { if (send_buffer) delete[] (char*)send_buffer; }

	const char* send_buffer;
	int send_buff_pos;
	int send_buff_len;
};

struct rttp_connection
{
	
	rttp_connection() : recv_buffer(NULL), recv_buff_pos(0), recv_buff_len(0), connected_time(0), create_time(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()) {}
	~rttp_connection() { if (recv_buffer) delete[] recv_buffer; }

	char* recv_buffer;
	int recv_buff_pos;
	int recv_buff_len;

	uint64_t connected_time;
	uint64_t create_time;
	
	std::deque<std::shared_ptr<socket_send_item>> send_deq;
};

struct rtsocket_server_context {
	bool run = true;
	SOCKET udp_socket = -1;
	uint64_t total_send = 0;
	uint64_t connection_num = 0;
	uint64_t total_waiting_send_num = 0;
	RTEngine engine = NULL;
	
	std::unordered_map<RTSOCKET, std::unique_ptr<rttp_connection>> connected_socket_map;

	char state[4096] = { 0 };
};

int server_main_func(rtsocket_server_context* ctx_ptr, int port);

