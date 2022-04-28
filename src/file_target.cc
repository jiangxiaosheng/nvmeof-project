#include "net.grpc.pb.h"
#include "net.pb.h"
#include "grpc/grpc.h"
#include "grpcpp/server_builder.h"
#include "grpcpp/security/server_credentials.h"
#include "third_party/thread_pool.hpp"

#include <memory>
#include <string>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unordered_map>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <cstring>
#include <sstream>
#include <mutex>

using namespace std;
using namespace net;
using namespace chrono;
namespace fs = std::filesystem;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using grpc::StatusCode;
using grpc::ServerCompletionQueue;
using grpc::ServerAsyncResponseWriter;
using grpc::ServerReaderWriter;

class GRPCFileServerSync final : public File::Service {
public:
	GRPCFileServerSync(string addr, int port) : address(addr), port(port) {}

	int run() {
		std::string server_addr = address + ":" + std::to_string(port);

		ServerBuilder builder;
		builder.AddListeningPort(server_addr, grpc::InsecureServerCredentials());
		builder.RegisterService(this);
		std::unique_ptr<Server> server(builder.BuildAndStart());
		std::cout << "Server listening on " << server_addr << std::endl;
  		server->Wait();

		return 0;
	}

	Status SetParameters(ServerContext *ctx, const Parameters *params, ACK *ack) {
		buffer_size = params->buffer_size();
		queue_depth = params->queue_depth();
		poll_threshold = params->poll_threshold();
		blk_size = params->blk_size();

		cout << "set params:\n" 
			<< "  buffer_size = " << buffer_size << "B\n"
			<< "  block_size = " << blk_size << "B\n"
			<< "  queue_depth = " << queue_depth << "\n"
			<< "  poll_threshold = " << poll_threshold << "\n"
			<< endl;
		return Status::OK;
	}

	Status AppendOneLargeFile(ServerContext *ctx, ServerReaderWriter<FileReply, FileRequest> *stream) {
		auto total_start = chrono::system_clock::now();

		FileRequest request;
		FileReply reply;
		stream->Read(&request);

		int err;

		int fd = open(request.filename().data(), O_APPEND | O_DIRECT | O_WRONLY | O_CREAT | O_TRUNC, 0644);

		decltype(chrono::system_clock::now()) rw_start, allocation_start;
		do {
			allocation_start = chrono::system_clock::now();
			void *buffer;
			err = posix_memalign(&buffer, blk_size, buffer_size);
			if (err) {
				fprintf(stderr, "Error posix_memalign: %s\n", strerror(-err));
				return Status(StatusCode::UNKNOWN, "posix_memalign failed");
			}
			memcpy(buffer, request.data().data(), buffer_size);
			time_on_allocation += chrono::system_clock::now() - allocation_start;
			
			rw_start = chrono::system_clock::now();
			err = write(fd, buffer, buffer_size);
			time_on_rw += chrono::system_clock::now() - rw_start;
			if (err < 0) {
				perror("write");
				return Status(StatusCode::UNKNOWN, "write data failed");
			}

			stream->Write(reply);
		} while (stream->Read(&request));
		
		time_total += std::chrono::system_clock::now() - total_start;
		cout << std::chrono::duration_cast<std::chrono::milliseconds>(time_total.time_since_epoch()).count() << endl;
		close(fd);
		return Status::OK;
	}

	// assume the data size equals to blksize
	// Status AppendManySmallFiles(ServerContext *ctx, ServerReaderWriter<FileRequest, FileReply> *stream) {
	// 	FileRequest request;
		
	// 	decltype(chrono::system_clock::now()) rw_start, allocation_start, open_start, total_start;

	// 	total_start = chrono::system_clock::now();
	// 	while (reader->Read(&request)) {	
	// 		fs::path full_path = fs::path(mnt_point) / fs::path(request.filename());
	// 		std::string filename = full_path.string();
			
	// 		open_start = chrono::system_clock::now();
	// 		int fd = open(filename.data(), O_APPEND | O_DIRECT | O_RDWR | O_CREAT, 0644);
	// 		time_on_open += chrono::system_clock::now() - open_start;
	// 		if (fd < 0) {
	// 			perror(("open file " + filename + " failed").data());
	// 			return Status(StatusCode::UNKNOWN, "open file failed");
	// 		}

	// 		allocation_start = chrono::system_clock::now();
	// 		char *buffer = new char[blksize + align];
	// 		buffer = (char *) (((uintptr_t) buffer + align) &~ ((uintptr_t) align));
	// 		memcpy(buffer, request.data().data(), request.data().size());
	// 		time_on_allocation += chrono::system_clock::now() - allocation_start;

	// 		rw_start = chrono::system_clock::now();
	// 		if (write(fd, buffer, blksize) != blksize) {
	// 			std::cerr << "write data failed" << std::endl;
	// 			return Status(StatusCode::UNKNOWN, "write data failed");
	// 		}
	// 		time_on_rw += chrono::system_clock::now() - rw_start;
	// 	}
	// 	time_total += chrono::system_clock::now() - total_start;

	// 	return Status::OK;
	// }

	Status GetStat(ServerContext *ctx, const StatRequest *request, StatReply *reply) {
		std::stringstream ss;
		ss << "time on allocation is " << std::chrono::duration_cast<std::chrono::milliseconds>(time_on_allocation.time_since_epoch()).count()
			<< " ms\n" << "time on writes is " 
			<< std::chrono::duration_cast<std::chrono::milliseconds>(time_on_rw.time_since_epoch()).count()
			<< " ms\n" << "time on open files is "
			<< std::chrono::duration_cast<std::chrono::milliseconds>(time_on_open.time_since_epoch()).count()
			<< " ms\n" << "total time on server is "
			<< std::chrono::duration_cast<std::chrono::milliseconds>(time_total.time_since_epoch()).count()
			<< " ms\n";
		cout << ss.str();
		reply->set_stat(ss.str());
		return Status::OK;
	}

	Status ResetStat(ServerContext *ctx, const StatRequest *request, StatReply *reply) {
		time_on_allocation = time_on_rw = time_total = time_on_open = {};
		return Status::OK;
	}

private:
	string address;
	int port;

	int queue_depth;
	int poll_threshold;
	decltype(std::chrono::system_clock::now()) time_on_allocation, time_on_rw, time_total, time_on_open;
	int buffer_size;
	int blk_size;
};

// class GRPCFileServerAsync final : public File::Service {
// public:
// 	GRPCFileServerAsync(string addr, int port, string mp, int bs) : address(addr), port(port), mnt_point(mp),
// 		blksize(bs), tp(std::make_unique<thread_pool>()) {}

// 	int run() {
// 		std::string server_addr = address + ":" + std::to_string(port);

// 		ServerBuilder builder;
// 		builder.AddListeningPort(server_addr, grpc::InsecureServerCredentials());
// 		builder.RegisterService(this);
// 		std::unique_ptr<Server> server(builder.BuildAndStart());
// 		std::cout << "Server listening on " << server_addr << std::endl;
//   		server->Wait();

// 		return 0;
// 	}

// 	Status AppendOneLargeFile(ServerContext *ctx, ServerReader<FileRequest>* reader, FileReply *reply) {
// 		auto total_start = chrono::system_clock::now();

// 		FileRequest request;
// 		reader->Read(&request);
// 		int fd;
// 		bool done = request.done();
// 		int err;
// 		int size = request.size();
// 		fs::path full_path = fs::path(mnt_point) / fs::path(request.filename());
		
// 		string filename = full_path.string();
// 		if (fd_map[filename]) {
// 			fd = fd_map.at(filename);
// 			if (done) {
// 				err = close(fd);
// 				if (err) {
// 					perror("close fd failed");
// 					return Status(StatusCode::UNKNOWN, "close fd failed");
// 				}
// 				fd_map.erase(filename);
// 				return Status::OK;
// 			}
// 		} else {
// 			if (done) {
// 				return Status(StatusCode::UNKNOWN, "file does not exist");
// 			}
// 			fd = open(filename.data(), O_APPEND | O_DIRECT | O_RDWR | O_CREAT | O_SYNC, 0644);
// 			fd_map[filename] = fd;
// 			if (fd < 0) {
// 				perror("open fd failed");
// 				return Status(StatusCode::UNKNOWN, "open fd failed");
// 			}
// 		}

// 		std::mutex mu;
// 		int count = 0;
// 		int align = blksize - 1;
// 		decltype(chrono::system_clock::now()) rw_start, allocation_start;

// 		do {
// 			allocation_start = chrono::system_clock::now();
// 			char *buffer = new char[blksize + align];
// 			buffer = (char *) (((uintptr_t) buffer + align) &~ ((uintptr_t) align));
// 			memcpy(buffer, request.data().data(), request.data().size());
// 			time_on_allocation += chrono::system_clock::now() - allocation_start;
			
// 			tp->push_task([&](int fd, char *buffer, int blksize) {
// 				mu.lock();
// 				auto start = chrono::system_clock::now();
// 				if (write(fd, buffer, blksize) != blksize) {
// 					perror("write file failed");
// 					mu.unlock();
// 					return;
// 				}
// 				time_on_rw += chrono::system_clock::now() - start;
// 				count++;
// 				if (count != 0 && count % 5000 == 0)
// 					std::cout << "write " << count << " times\n";
// 				mu.unlock();
// 			}, fd, buffer, blksize);
// 		} while (reader->Read(&request));

// 		tp->wait_for_tasks();
// 		time_total += std::chrono::system_clock::now() - total_start;
// 		return Status::OK;
// 	}

// 	Status AppendManySmallFiles(ServerContext *ctx, ServerReader<FileRequest>* reader, FileReply *reply) {
// 		FileRequest request;
// 		int align = blksize - 1;
// 		decltype(chrono::system_clock::now()) rw_start, allocation_start, open_start, total_start;

// 		int count = 0;
// 		std::mutex mu;
// 		total_start = chrono::system_clock::now();
// 		while (reader->Read(&request)) {	
// 			fs::path full_path = fs::path(mnt_point) / fs::path(request.filename());
// 			std::string filename = full_path.string();
			
// 			open_start = chrono::system_clock::now();
// 			int fd = open(filename.data(), O_APPEND | O_DIRECT | O_RDWR | O_CREAT | O_SYNC, 0644);
// 			time_on_open += chrono::system_clock::now() - open_start;
// 			if (fd < 0) {
// 				perror(("open file " + filename + " failed").data());
// 				return Status(StatusCode::UNKNOWN, "open file failed");
// 			}

// 			allocation_start = chrono::system_clock::now();
// 			char *buffer = new char[blksize + align];
// 			buffer = (char *) (((uintptr_t) buffer + align) &~ ((uintptr_t) align));
// 			memcpy(buffer, request.data().data(), request.data().size());
// 			time_on_allocation += chrono::system_clock::now() - allocation_start;

// 			tp->push_task([&](int fd, char *buffer, int blksize) {
// 				auto start = chrono::system_clock::now();
// 				if (write(fd, buffer, blksize) != blksize) {
// 					perror("write file failed");
// 					return;
// 				}
// 				auto past = chrono::system_clock::now() - start;
// 				mu.lock();
// 				time_on_rw += past;
// 				count++;
// 				if (count != 0 && count % 5000 == 0)
// 					std::cout << "write " << count << " times\n";
// 				mu.unlock();
// 			}, fd, buffer, blksize);
// 		}

// 		tp->wait_for_tasks();
// 		time_total += chrono::system_clock::now() - total_start;

// 		return Status::OK;
// 	}

	// Status GetStat(ServerContext *ctx, const StatRequest *request, StatReply *reply) {
	// 	std::stringstream ss;
	// 	ss << "time on allocation is " << std::chrono::duration_cast<std::chrono::milliseconds>(time_on_allocation.time_since_epoch()).count()
	// 		<< " ms\n" << "time on writes is " 
	// 		<< std::chrono::duration_cast<std::chrono::milliseconds>(time_on_rw.time_since_epoch()).count()
	// 		<< " ms\n" << "time on open files is "
	// 		<< std::chrono::duration_cast<std::chrono::milliseconds>(time_on_open.time_since_epoch()).count()
	// 		<< " ms\n" << "total time on server is "
	// 		<< std::chrono::duration_cast<std::chrono::milliseconds>(time_total.time_since_epoch()).count()
	// 		<< " ms\n";
	// 	reply->set_stat(ss.str());
	// 	return Status::OK;
	// }

// 	Status ResetStat(ServerContext *ctx, const StatRequest *request, StatReply *reply) {
// 		time_on_allocation = time_on_rw = time_total = time_on_open = {};
// 		return Status::OK;
// 	}

// private:
// 	string address;
// 	int port;
// 	string mnt_point;
// 	unordered_map<string, int> fd_map;
// 	decltype(std::chrono::system_clock::now()) time_on_allocation, time_on_rw, time_total, time_on_open;
// 	int blksize;
// 	std::unique_ptr<thread_pool> tp;
// };

int main(int argc, char **argv) {
	GRPCFileServerSync server("0.0.0.0", 9876);
	// GRPCFileServerAsync server("0.0.0.0", 9876, "/mnt", 4096);
	server.run();
}