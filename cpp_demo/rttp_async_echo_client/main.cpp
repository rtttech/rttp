#include "rttp_async_echo_client.h"
#include <string>
#include <thread>
#include <chrono>

using namespace std;

int main(int argc, char* argv[])
{
	
	int interval = 500;//ms
	int packet_num = 3;

	if (argc < 3) {
		cout << "usage: program <remote ip> <remote port> [interval] [packet_num]" << endl;
		return 0;
	}

	if (argc >= 4) {
		interval = atoi(argv[3]);
	}

	if (argc >= 5) {
		packet_num = atoi(argv[4]);
	}

	rtsocket_client_context ctx;

	std::thread t(rttp_client_main_func, &ctx, argv[1], atoi(argv[2]), interval, packet_num);

	while (ctx.run) {
		cout << "rttp: " << ctx.avg_latency / 1000 << " milli seconds " << ctx.state << endl;

		this_thread::sleep_for(std::chrono::milliseconds(500));
	}

	t.join();

	return 0;
}