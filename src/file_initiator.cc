#include "net.grpc.pb.h"
#include "net.pb.h"
#include "grpc/grpc.h"
#include "grpcpp/create_channel.h"
#include "third_party/argparse.hpp"

#include <string>
#include <chrono>

using namespace grpc;
using namespace net;
using namespace std;
using namespace chrono;

string server_addr;
int server_port;
size_t buffer_size;
int N;
int queue_depth;
int idle_threshold;
bool large;
bool poll;
bool closed_loop;
bool test_read;

void parse_args(int argc, char **argv) {
	argparse::ArgumentParser program;
	program.add_argument("-d").help("io_uring queue depth").default_value(128).scan<'i', int>();
	program.add_argument("-b").help("the buffer size for each read/write").default_value(4096).scan<'i', int>();
	program.add_argument("-n").help("the times we repeat reading/writing").default_value(10'000).scan<'i', int>();
	program.add_argument("-t").help("the threshold (ms) for io_uring to re-enter an sqe in poll mode").default_value(2000).scan<'i', int>();
	program.add_argument("-l").help("append to/read from one large file").default_value(false).implicit_value(true);
	program.add_argument("-p").help("use io_uring poll mode").default_value(false).implicit_value(true);
	program.add_argument("-c").help("test with closed loop (normal blocking read/write syscall rather than io_uring)").default_value(false).implicit_value(true);
	program.add_argument("-r").help("test reading, default is writing").default_value(false).implicit_value(true);
	program.add_argument("-addr").help("the ip address of the target").default_value("10.10.1.2");
	program.add_argument("-port").help("the port of the target").default_value(9876).scan<'i', int>();
	program.parse_args(argc, argv);

	queue_depth = program.get<int>("-d");
	buffer_size = program.get<int>("-b");
	N = program.get<int>("-n");
	idle_threshold = program.get<int>("-t");
	large = program.get<bool>("-l");
	poll = program.get<bool>("-p");
	closed_loop = program.get<bool>("-c");
	test_read = program.get<bool>("-r");
	server_addr = program.get<string>("-addr");
	server_port = program.get<int>("-port");
}

int main(int argc, char **argv) {
	string endpoint = server_addr + ":" + to_string(server_port);
	auto channel = CreateChannel(endpoint, InsecureChannelCredentials());
	auto stub = File::NewStub(channel);
	FileReply reply;

	char *buffer = new char[buffer_size];
	memset(buffer, 0, buffer_size);

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

	// ClientContext ctx_many_small_files;
	// auto writer_small_files = stub->AppendManySmallFiles(&ctx_many_small_files, &reply);

	// total_time = {};
	// start_time = system_clock::now();
	// for (int i = 0; i < N; i++) {
	// 	FileRequest request;

	// 	request.set_filename("small-file" + std::to_string(i));
	// 	request.set_data(buffer);
	// 	request.set_size(4096);

	// 	if (!writer_small_files->Write(request)) {
	// 		cerr << "write file data failed" << endl;
	// 		break;
	// 	}
	// }
	// writer_small_files->WritesDone();
 	// Status status = writer_small_files->Finish();
	// if (!status.ok()) {
	// 	cout << "write remote file failed" << endl;
	// 	cerr << status.error_message() << endl;
	// 	return -1;
	// }
	// total_time += system_clock::now() - start_time;

	// printf("append remote many small files\n");
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
}