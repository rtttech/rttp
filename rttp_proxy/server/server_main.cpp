#include <uv.h>
#include <iostream>
#include <thread>

#include "rttp_server.h"

int main(int argc, char* argv[])
{
	if (argc < 6)
	{
		std::cerr << "usage: RttpProxyServer <local host ip> <local port> <forward host ip> <forward port> <max connections> [http monitor port]" << std::endl;
		return 1;
	}

	const unsigned short local_port = static_cast<unsigned short>(::atoi(argv[2]));
	const unsigned short forward_port = static_cast<unsigned short>(::atoi(argv[4]));
	const std::string local_host = argv[1];
	const std::string forward_host = argv[3];
	const std::size_t max_connections = static_cast<std::size_t>(::atoi(argv[5]));
	unsigned short http_port = local_port + 1000;
	if (argc >= 7) {
		http_port = static_cast<unsigned short>(::atoi(argv[6]));
	}

	// Create libuv event loop
	uv_loop_t* loop = uv_loop_new();
	if (!loop) {
		std::cerr << "Failed to create libuv loop" << std::endl;
		return 1;
	}

	
	bridge_param param;
	param.max_connections = (int)max_connections;
	param.upstream_address.push_back(std::make_pair(forward_host, forward_port));

	rttp_server server(loop, local_host, local_port, param, http_port);
	if (server.start() != 0) {
		uv_loop_delete(loop);
		return 1;
	}

	std::cout << "RTTP Proxy Server started on " << local_host << ":" << local_port << std::endl;
	std::cout << "Forwarding to " << forward_host << ":" << forward_port << std::endl;


	// Run event loop
	int result = uv_run(loop, UV_RUN_DEFAULT);

	uv_loop_delete(loop);

	std::cout << "terminated" << std::endl;

	return result;
}
