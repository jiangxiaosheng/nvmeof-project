#include "net.grpc.pb.h"
#include "net.pb.h"
#include "grpc/grpc.h"
#include "grpcpp/create_channel.h"

#include <string>

using namespace grpc;
using namespace net;
using namespace std;

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
}