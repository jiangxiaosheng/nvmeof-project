#include "net.grpc.pb.h"
#include "net.pb.h"
#include "grpc/grpc.h"
#include "grpcpp/server_builder.h"
#include "grpcpp/security/server_credentials.h"

#include <memory>
#include <string>
#include <fcntl.h>
#include <sys/types.h>
#include <unordered_map>
#include <chrono>

using namespace std;
using namespace net;
using namespace chrono;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using grpc::StatusCode;
using grpc::ServerCompletionQueue;
using grpc::ServerAsyncResponseWriter;

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

	Status AppendOneFile(ServerContext *ctx, const FileRequest *request, FileReply *reply) {
		int fd;
		string filename = move(request->filename());
		bool done = request->done();
		int err;
		int size = request->size();
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
			fd = open(filename.data(), O_APPEND | O_DIRECT | O_RDWR);
			if (fd < 0) {
				perror("open fd failed");
				return Status(StatusCode::UNKNOWN, "open fd failed");
			}
		}

		err = write(fd, request->data().data(), size);
		return Status::OK;

	}

private:
	string address;
	int port;
	unordered_map<string, int> fd_map;
	decltype(system_clock::now()) time_on_writes;
};

int main(int argc, char **argv) {
	GRPCFileServerSync server("0.0.0.0", 9876);
	server.run();
}