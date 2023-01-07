#include "file_server.h"


int main(int argc, char **argv) {
	GRPCFileServerSync server("0.0.0.0", 9876);
	server.run();
}