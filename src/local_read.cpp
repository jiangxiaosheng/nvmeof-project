#include <cerrno>
#include "fcntl.h"
#include <chrono>
#include <libgen.h>
#include <string>
#include "common.hpp"
#include <cstring>
#include <random>

using namespace std;

const int N = 1'000'000;
// const int N = 1;

long random_start_block() {
	static random_device rd;
    static mt19937 mt(rd());
    static uniform_int_distribution<int> dist(0, INT32_MAX);

	return dist(mt) % config.number_of_logical_blocks;
}

void test_write_throughput_random()
{
	decltype(chrono::system_clock::now()) start_time, end_time, total_time;
	int fd, err;
	char *buffer;

	fd = open_bdev(config.name, O_WRONLY | O_DIRECT);

	struct nvme_io_args args;
	args.args_size = sizeof(args);
	args.fd = fd;
	args.nsid = config.namespace_id;
	args.nlb = 1;
	args.data_len = config.logical_block_size;
	args.result = NULL;

	for (int i = 0; i < N; i++) {
		buffer = (char *) malloc(config.logical_block_size);
		args.data = buffer;
		args.slba = random_start_block();
		// memcpy(args.data, "123\n", 4);
		// if (i % 10000 == 0 && i != 0)
		// 	printf("%d blocks written\n", i);

		// printf("write block %llu\n", args.slba);
		start_time = chrono::system_clock::now();
		err = nvme_write(&args);
		end_time = chrono::system_clock::now();
		if (err)
			perror("nvme write failed");
		
		total_time += end_time - start_time;
		
		free(buffer);
	}

	printf("total time for random write is %lu microseconds\n", chrono::duration_cast<chrono::microseconds>(total_time.time_since_epoch()).count());
	printf("total data written is %u bytes\n", N * config.logical_block_size);
	printf("throughput for random writting (512 bytes) is %f Gb/s\n", (N * config.logical_block_size) * 1.0 / 1024 / 1024 / 1024 / 
		chrono::duration_cast<chrono::microseconds>(total_time.time_since_epoch()).count() * 1e6);
	printf("IOPS for random writting (512 bytes) is %f\n", N * 1.0 / chrono::duration_cast<chrono::microseconds>(total_time.time_since_epoch()).count() * 1e6);
}

void test_write_throughput_sequential()
{
}

int main(int argc, const char **argv)
{
	config.name = (char *)argv[1];
	// config.namespace_id = 0x1;

	init_device_config();

	test_write_throughput_random();
}