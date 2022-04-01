#include <cerrno>
#include "fcntl.h"
#include <chrono>
#include <libgen.h>
#include <string>
#include "common.hpp"

using namespace std;

const int N = 1'000'000;


void test_write_throughput_random()
{
	decltype(chrono::system_clock::now()) start_time, end_time, total_time;
	void *buffer = nullptr;
	int err = 0;
	int mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH;
	int logical_block_size = 0;
	long long buffer_size = 0;
	bool huge;
	nvme_id_ns ns{};
	uint32_t total_blocks;
	int fd;

	fd = open_bdev(config.name, O_RDWR | O_DIRECT);

	err = nvme_get_logical_block_size(fd, config.namespace_id, &logical_block_size);
	if (err)
		perror("get logical block size failed");
	else
		printf("the block size of device %s is %d\n", config.name, logical_block_size);
	

}

void test_write_throughput_sequential()
{
}

int main(int argc, const char **argv)
{
	config.name = "/dev/nvme1n1";
	config.namespace_id = 0x1;

	init_device_config();

	// test_write_throughput_random();
}