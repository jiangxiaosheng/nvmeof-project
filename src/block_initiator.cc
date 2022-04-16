#include "net.h"
#include "nvme.h"
#include <chrono>
#include <unistd.h>
#include <random>

#include "grpc/grpc.h"
#include <grpcpp/create_channel.h>

using namespace std;
using namespace grpc;
using namespace net;
using namespace chrono;

const int N = 1'000'0;

long random_start_block(int total_blocks) {
	static random_device rd;
    static mt19937 mt(rd());
    static uniform_int_distribution<int> dist(0, INT32_MAX);

	return dist(mt) % total_blocks;
}

int main(int argc, const char **argv) {
	int data_size;
	string addr = "0.0.0.0";
	int port = 9876;
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
	auto stub = Block::NewStub(channel);

	BlockRequest request;
	BlockReply reply;
	request.set_size(10);
	request.set_data("123");
	ClientContext ctx;
	CompletionQueue cq;
	Status status;
	unique_ptr<ClientAsyncResponseReader<BlockReply>> rpc(stub->PrepareAsyncWriteBlock(&ctx, request, &cq));
	rpc->StartCall();
	rpc->Finish(&reply, &status, (void *)1);
	void *got_tag;
	bool ok = false;
	std::this_thread::sleep_for(chrono::seconds(5));

	GPR_ASSERT(cq.Next(&got_tag, &ok));
	GPR_ASSERT(got_tag == (void*)1);
	GPR_ASSERT(ok);
	

	if (status.ok()) {
      cout << reply.end_block() << endl;
    } else {
      cout << "RPC failed" << endl;
    }

	// char *buffer = new char[data_size];
	// BlockRequest request;
	// BlockReply reply;
	// Status status;
	// decltype(system_clock::now()) start_time, end_time, total_time;
	// int start_block = 0;
	
	// for (int i = 0; i < N; i++) {
	// 	if (i != 0 && i % 1000 == 0)
	// 		cout << "write " << i << " times" << std::endl;

	// 	ClientContext context;

	// 	request.set_size(data_size);
	// 	request.set_data(buffer);
	// 	request.set_start_block(random_start_block(config.number_of_logical_blocks));
		
	// 	start_time = system_clock::now();
	// 	status = stub->WriteBlock(&context, request, &reply);
	// 	if (!status.ok()) {
	// 		cout << "write remote block failed" << endl;
	// 		cerr << status.error_message() << std::endl;
	// 		return -1;
	// 	}
	// 	total_time += system_clock::now() - start_time;
	// }

	// // get server stats
	// {
	// 	ClientContext context;
	// 	StatRequest request;
	// 	StatReply reply;
	// 	stub->GetStat(&context, request, &reply);
	// 	cout << "server stats: " << endl;
	// 	cout << reply.stat() << endl;
	// }

	// printf("total time for random write is %lu milliseconds\n", chrono::duration_cast<chrono::milliseconds>(total_time.time_since_epoch()).count());
	// printf("total data written is %u bytes\n", N * data_size);
	// printf("throughput for random writting (%d bytes) is %f Mb/s\n", data_size, (N * data_size) * 1.0 / 1024 / 1024 / 
	// 	chrono::duration_cast<chrono::microseconds>(total_time.time_since_epoch()).count() * 1e6);
	// printf("IOPS for random writting (%d bytes) is %f\n", data_size, N * 1.0 / chrono::duration_cast<chrono::microseconds>(total_time.time_since_epoch()).count() * 1e6);	

}