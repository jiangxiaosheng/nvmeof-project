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
	FileReply reply;

	int N = 50000;

	char *buffer = new char[4096];
	memset(buffer, 0, 4096);

	decltype(system_clock::now()) start_time, end_time, total_time;

	// ========================================== append one large file ===================================

	// ClientContext context;
	// auto writer = stub->AppendOneLargeFile(&context, &reply);

	// start_time = system_clock::now();
	// for (int i = 0; i < N; i++) {
	// 	FileRequest request;
		
	// 	request.set_filename("large_file");
	// 	request.set_data(buffer);
	// 	request.set_size(4096);

		
	// 	if (!writer->Write(request)) {
	// 		cerr << "write file data failed" << endl;
	// 		break;
	// 	}
	// }
	// writer->WritesDone();
	// Status status = writer->Finish();
	// if (!status.ok()) {
	// 	cout << "write remote file failed" << endl;
	// 	cerr << status.error_message() << endl;
	// 	return -1;
	// }
	
	// total_time += system_clock::now() - start_time;

	// printf("append remote one large file\n");
	// printf("total time for appending is %lu milliseconds\n", chrono::duration_cast<chrono::milliseconds>(total_time.time_since_epoch()).count());
	// printf("total data written is %u bytes\n", N * 4096);
	// printf("throughput for appending (%d bytes) is %f Mb/s\n", 4096, (N * 4096) * 1.0 / 1024 / 1024 / 
	// 	chrono::duration_cast<chrono::microseconds>(total_time.time_since_epoch()).count() * 1e6);
	// printf("IOPS for appending (%d bytes) is %f\n", 4096, N * 1.0 / chrono::duration_cast<chrono::microseconds>(total_time.time_since_epoch()).count() * 1e6);

	// {
	// 	StatRequest stat_request;
	// 	StatReply stat_reply;
	// 	ClientContext stat_context;
	// 	stub->GetStat(&stat_context, stat_request, &stat_reply);
	// 	cout << "latency breakdown in the target side:" << endl;
	// 	cout << stat_reply.stat() << endl;

	// 	ClientContext reset_stat_ctx;
	// 	stub->ResetStat(&reset_stat_ctx, stat_request, &stat_reply);
	// }
	

	// ========================================== append many small files ===================================

	ClientContext ctx_many_small_files;
	auto writer_small_files = stub->AppendManySmallFiles(&ctx_many_small_files, &reply);

	total_time = {};
	start_time = system_clock::now();
	for (int i = 0; i < N; i++) {
		FileRequest request;

		request.set_filename("small-file" + std::to_string(i));
		request.set_data(buffer);
		request.set_size(4096);

		if (!writer_small_files->Write(request)) {
			cerr << "write file data failed" << endl;
			break;
		}
	}
	writer_small_files->WritesDone();
 	Status status = writer_small_files->Finish();
	if (!status.ok()) {
		cout << "write remote file failed" << endl;
		cerr << status.error_message() << endl;
		return -1;
	}
	total_time += system_clock::now() - start_time;

	printf("append remote many small files\n");
	printf("total time for appending is %lu milliseconds\n", chrono::duration_cast<chrono::milliseconds>(total_time.time_since_epoch()).count());
	printf("total data written is %u bytes\n", N * 4096);
	printf("throughput for appending (%d bytes) is %f Mb/s\n", 4096, (N * 4096) * 1.0 / 1024 / 1024 / 
		chrono::duration_cast<chrono::microseconds>(total_time.time_since_epoch()).count() * 1e6);
	printf("IOPS for appending (%d bytes) is %f\n", 4096, N * 1.0 / chrono::duration_cast<chrono::microseconds>(total_time.time_since_epoch()).count() * 1e6);

	{
		StatRequest stat_request;
		StatReply stat_reply;
		ClientContext stat_context;
		stub->GetStat(&stat_context, stat_request, &stat_reply);
		cout << "latency breakdown in the target side:" << endl;
		cout << stat_reply.stat() << endl;

		ClientContext reset_stat_ctx;
		stub->ResetStat(&reset_stat_ctx, stat_request, &stat_reply);
	}
}