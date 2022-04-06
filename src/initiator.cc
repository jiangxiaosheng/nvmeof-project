#include "net.h"
#include "nvme.h"
#include <chrono>
#include <unistd.h>

#include "grpc/grpc.h"
#include <grpcpp/create_channel.h>

using namespace std;
using namespace grpc;
using namespace block;

const int N = 1'000'000;

int main(int argc, const char **argv) {
	int data_size;
	string addr = "0.0.0.0";
	int port;
	if (argc >= 3)
		addr = argv[2];
	if (argc >= 4)
		port = atoi(argv[3]);
	if (argc == 5)
		data_size = atoi(argv[4]);

	struct device_config config;
	config.name = (char *)argv[1];

	init_device_config(config);
	if (argc < 5)
		data_size = config.logical_block_size;

	string server_addr = addr + ":" + to_string(port);

	auto channel = CreateChannel(server_addr, InsecureChannelCredentials());
	auto stub = block::Block::NewStub(channel);

	ClientContext context;
	BlockRequest request;
	request.set_size(data_size);
	request.set_data("123");
	request.set_start_block(0);
	BlockReply reply;
	Status status = stub->WriteBlock(&context, request, &reply);
}