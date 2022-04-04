#include "net.h"
#include <chrono>
#include <random>
#include "nvme.h"

using namespace std;

const int N = 1'000'000;

long random_start_block() {
	static random_device rd;
    static mt19937 mt(rd());
    static uniform_int_distribution<int> dist(0, INT32_MAX);

	return dist(mt) % config.number_of_logical_blocks;
}

void test_write_throughput_random(Connection &conn) {
	decltype(chrono::system_clock::now()) start_time, end_time, total_time;
	int fd, err;
	char buffer[512];
	int count = 0;

	fd = open_bdev(config.name, O_WRONLY | O_DIRECT);

	struct nvme_io_args args;
	args.args_size = sizeof(args);
	args.fd = fd;
	args.nsid = config.namespace_id;
	args.nlb = 0;
	args.data_len = config.logical_block_size;
	args.result = NULL;

	// start_time = chrono::system_clock::now();
	for (int i = 0; i < N; i++) {
		int size = conn.recv(buffer, config.logical_block_size);
		if (size < 0) {
			perror("recv failed");
			break;
		}

		if (i != 0 && i % 50000 == 0)
			cout << "write " << i << " blocks" << endl;
		
		args.data = buffer;
		// args.slba = random_start_block();
		args.slba = 0;

		err = nvme_write(&args);
		
		if (err < 0) {
			fprintf(stderr, "submit-io: %s\n", nvme_strerror(errno));
			return;
		} else if (err) {
			nvme_show_status(err);
			return;
		}
		
		count++;
	}

	conn.send((void *) string("done").c_str(), 5);
	// end_time = chrono::system_clock::now();
	// total_time += end_time - start_time;

	// printf("total time for random write is %lu microseconds\n", chrono::duration_cast<chrono::microseconds>(total_time.time_since_epoch()).count());
	// printf("total data written is %u bytes\n", count * config.logical_block_size);
	// printf("throughput for random writting (512 bytes) is %f Gb/s\n", (count * config.logical_block_size) * 1.0 / 1024 / 1024 / 1024 / 
	// 	chrono::duration_cast<chrono::microseconds>(total_time.time_since_epoch()).count() * 1e6);
	// printf("IOPS for random writting (512 bytes) is %f\n", count * 1.0 / chrono::duration_cast<chrono::microseconds>(total_time.time_since_epoch()).count() * 1e6);	
}

int main(int argc, const char **argv) {
	string device = "mlx5_0";
	string addr = "0.0.0.0";
	if (argc >= 3)
		addr = argv[2];
	if (argc == 4)
		device = argv[3];
	TCPConnection conn(TARGET, addr, 9875);
	conn.init();

	config.name = (char *)argv[1];

	init_device_config();

	test_write_throughput_random(conn);
}