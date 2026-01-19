#pragma once

#include "../api/rtsocket.h"

#include <iostream>
#include <deque>
#include <set>
#include <map>
#include <unordered_map>
#include <assert.h>
#include <string.h>
#include <random>
#include <time.h>
#include <memory>
#include <numeric>
#include <thread>
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
	socket_send_item(const char* buffer, int len, int pos = 0) : send_buffer(buffer), send_buff_len(len), send_buff_pos(pos) {}
	~socket_send_item() { delete[] send_buffer; }

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

struct rtsocket_client_context
{
	std::thread *thread_ptr = NULL;
	bool run = true;
	SOCKET socket = -1;
	uint64_t total_received_udp_pkt = 0;
	uint64_t connected_socket_num = 0;
	std::deque<uint32_t> latency_deq;
	uint64_t avg_latency = 0;

	RTEngine rttp_engine = NULL;

	std::unordered_map<RTSOCKET, std::shared_ptr<rttp_connection>> socket_map;
	std::unordered_map<uint32_t, sent_packet_info> sent_packet_info_map;

	char state[4096] = {0};
};

int rttp_client_main_func(rtsocket_client_context* ctx_ptr, const char* remote_addr, int port, int interval, int packet_num);
