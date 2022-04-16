#include "net.grpc.pb.h"
#include "net.pb.h"
#include "grpc/grpc.h"
#include "grpcpp/server_builder.h"
#include "grpcpp/security/server_credentials.h"

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
using grpc::ServerReader;

class GRPCFileServerSync final : public File::Service {
public:
	GRPCFileServerSync(string addr, int port, string mp, int bs) : address(addr), port(port), mnt_point(mp),
		blksize(bs) {}

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

	Status AppendOneFile(ServerContext *ctx, ServerReader<FileRequest>* reader, FileReply *reply) {
		auto total_start = chrono::system_clock::now();

		FileRequest request;
		reader->Read(&request);
		int fd;
		bool done = request.done();
		int err;
		int size = request.size();
		fs::path full_path = fs::path(mnt_point) / fs::path(request.filename());
		
		string filename = full_path.string();
		if (fd_map[filename]) {
			fd = fd_map.at(filename);
			if (done) {
				err = close(fd);
				if (err) {
					perror("close fd failed");
					return Status(StatusCode::UNKNOWN, "close fd failed");
				}
				fd_map.erase(filename);
				return Status::OK;
			}
		} else {
			if (done) {
				return Status(StatusCode::UNKNOWN, "file does not exist");
			}
			fd = open(filename.data(), O_APPEND | O_DIRECT | O_RDWR | O_CREAT, 0600);
			if (fd < 0) {
				perror("open fd failed");
				return Status(StatusCode::UNKNOWN, "open fd failed");
			}
		}

		int align = blksize - 1;
		decltype(chrono::system_clock::now()) rw_start, allocation_start;
		do {
			allocation_start = chrono::system_clock::now();
			char *buffer = new char[blksize + align];
			buffer = (char *) (((uintptr_t) buffer + align) &~ ((uintptr_t) align));
			memcpy(buffer, request.data().data(), request.data().size());
			time_on_allocation += chrono::system_clock::now() - allocation_start;
			
			rw_start = chrono::system_clock::now();
			err = write(fd, buffer, blksize);
			time_on_rw += chrono::system_clock::now() - rw_start;
			if (err < 0) {
				std::cerr << "write data to file " << filename << " failed: " << strerror(errno) << std::endl;
				return Status(StatusCode::UNKNOWN, "write data failed");
			}
		} while (reader->Read(&request));
		
		time_total += std::chrono::system_clock::now() - total_start;
		return Status::OK;
	}

	Status GetStat(ServerContext *ctx, const StatRequest *request, StatReply *reply) {
		std::stringstream ss;
		ss << "time on allocation is " << std::chrono::duration_cast<std::chrono::milliseconds>(time_on_allocation.time_since_epoch()).count()
			<< " ms\n" << "time on writes is " 
			<< std::chrono::duration_cast<std::chrono::milliseconds>(time_on_rw.time_since_epoch()).count()
			<< " ms\n" << "total time on server is "
			<< std::chrono::duration_cast<std::chrono::milliseconds>(time_total.time_since_epoch()).count()
			<< " ms\n";
		reply->set_stat(ss.str());
		return Status::OK;
	}

private:
	string address;
	int port;
	string mnt_point;
	unordered_map<string, int> fd_map;
	decltype(std::chrono::system_clock::now()) time_on_allocation, time_on_rw, time_total;
	int blksize;
};

int main(int argc, char **argv) {
	GRPCFileServerSync server("0.0.0.0", 9876, "/mnt", 4096);
	server.run();
	// string file = "/mnt/test";
	// struct stat fstat;
	// stat(file.data(), &fstat);
	// int blksize = (int) fstat.st_blksize;
	// int align = blksize - 1;
	// cout << blksize << endl;

	// char *buffer = new char[blksize + align];
	// cout << (void *) buffer << endl;
	// buffer = (char *) (((uintptr_t) buffer + align) &~ ((uintptr_t) align));
	// cout << (void *) buffer << endl;
	// memcpy(buffer, "123", 4);
	// int fd = open(file.data(), O_RDWR | O_CREAT | O_APPEND | O_DIRECT, 0600);
	// if (fd < 0)
	// 	perror("open failed");
	// int size = write(fd, buffer, (size_t) blksize);
	// if (size < 0)
	// 	perror("write failed");
	// delete[] buffer;
}