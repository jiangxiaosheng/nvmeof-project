#include "net.h"

using namespace std;

int main(int argc, const char **argv) {
	string device {argv[1]};
	string addr = "";
	if (argc == 3)
		addr = argv[2];
	init_network(device, TARGET, addr);
}