#include "net.grpc.pb.h"
#include "net.pb.h"

#include <memory>
#include <string>
#include <fcntl.h>
#include <sys/types.h>
#include <unordered_map>

using namespace std;
using namespace net;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using grpc::StatusCode;
using grpc::ServerCompletionQueue;
using grpc::ServerAsyncResponseWriter;

class GRPCFileServerSync final : public File::Service {
public:
	GRPCFileServerSync(string addr, int port) : address(addr), port(port) {

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
};

int main(int argc, char **argv) {
	int fd = open("test.txt", O_CREAT | O_RDWR | O_APPEND);
	write(fd, "123", 4);
	close(fd);
}