#include "rttp_async_echo_server.h"

#include <iostream>
#include <thread>
#include <chrono>

using namespace std;

int main(int argc, char* argv[])
{
	if (argc != 2) {
		cout << "usage: program <listen port>";
		return 0;
	}

	rtsocket_server_context ctx;
	
	u_short port = atoi(argv[1]);

	std::thread t1(server_main_func, &ctx, port);

	while (ctx.run) {
		this_thread::sleep_for(std::chrono::milliseconds(1000));
		cout << "conn num: " << ctx.connection_num
			<< " waiting send num: " << ctx.total_waiting_send_num
			<< " state: "<<ctx.state << endl;
	}


	t1.join();

	return 0;
}