#include "net.grpc.pb.h"
#include "net.pb.h"
#include "grpc/grpc.h"
#include "grpcpp/create_channel.h"

#include <string>
#include <chrono>

using namespace grpc;
using namespace net;
using namespace std;
using namespace chrono;

int main(int argc, char **argv) {
	string addr = "0.0.0.0";
	if (argc >= 2)
		addr = argv[1];
	int port = 9876;
	if (argc >= 3)
		port = atoi(argv[2]);

	string server_addr = addr + ":" + to_string(port);
	auto channel = CreateChannel(server_addr, InsecureChannelCredentials());
	auto stub = File::NewStub(channel);
	Status status;

	int N = 1000;

	char *buffer = new char[4096];

	decltype(system_clock::now()) start_time, end_time, total_time;

	for (int i = 0; i < N; i++) {
		ClientContext context;
		FileRequest request;
		FileReply reply;
		
		request.set_filename("large_file");
		request.set_data(buffer);
		request.set_size(4096);

		start_time = system_clock::now();
		status = stub->AppendOneFile(&context, request, &reply);
		if (!status.ok()) {
			cout << "write remote file failed" << endl;
			cerr << status.error_message() << endl;
			return -1;
		}
		total_time += system_clock::now() - start_time;
	}

	printf("total time for appending is %lu milliseconds\n", chrono::duration_cast<chrono::milliseconds>(total_time.time_since_epoch()).count());
	printf("total data written is %u bytes\n", N * 4096);
	printf("throughput for appending (%d bytes) is %f Mb/s\n", 4096, (N * 4096) * 1.0 / 1024 / 1024 / 
		chrono::duration_cast<chrono::microseconds>(total_time.time_since_epoch()).count() * 1e6);
	printf("IOPS for appending (%d bytes) is %f\n", 4096, N * 1.0 / chrono::duration_cast<chrono::microseconds>(total_time.time_since_epoch()).count() * 1e6);	

}