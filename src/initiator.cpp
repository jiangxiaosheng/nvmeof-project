#include "net.h"
#include "nvme.h"
#include <chrono>
#include <unistd.h>

using namespace std;

const int N = 1'000'000;

int main(int argc, const char **argv) {
	int data_size;
	string addr = "0.0.0.0";
	if (argc >= 3)
		addr = argv[2];
	if (argc == 4)
		data_size = atoi(argv[3]);
	TCPConnection conn(INITIATOR, addr, 9875);
	conn.init();

	config.name = (char *)argv[1];

	init_device_config();
	if (argc < 4)
		data_size = config.logical_block_size;

	decltype(chrono::system_clock::now()) start_time, end_time, total_time;

	char *buffer = new char[data_size];

	start_time = chrono::system_clock::now();
	for (int i = 0; i < N; i++) {
		conn.send(buffer, data_size);
	}

	conn.recv(buffer, 5);
	if (strcmp(buffer, "done") != 0) {
		delete[] buffer;
		cerr << "error" << endl;
		return -1;
	}

	end_time = chrono::system_clock::now();
	total_time += end_time - start_time;
	delete[] buffer;

	printf("total time for random write is %lu microseconds\n", 
		chrono::duration_cast<chrono::microseconds>(total_time.time_since_epoch()).count());
	printf("total data written is %u bytes\n", N * config.logical_block_size);
	printf("throughput for random writting (512 bytes) is %f Gb/s\n", (N * config.logical_block_size) * 1.0 / 1024 / 1024 / 1024 / 
		chrono::duration_cast<chrono::microseconds>(total_time.time_since_epoch()).count() * 1e6);
	printf("IOPS for random writting (512 bytes) is %f\n", N * 1.0 / chrono::duration_cast<chrono::microseconds>(total_time.time_since_epoch()).count() * 1e6);

}