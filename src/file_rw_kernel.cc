#include <iostream>
#include <chrono>
#include <string>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>

using namespace std;
using namespace chrono;

int main(int argc, char **argv) {
	int blksize = 4096;
	int align = blksize - 1;
	char *buffer = new char[blksize + align];
	buffer = (char *) (((uintptr_t) buffer + align) &~ ((uintptr_t) align));

	int N = 20000;

	decltype(system_clock::now()) start_time, end_time, total_time;

	// ================ append one large file ====================
	string file = "/mnt/nvmeof-largefile";
	int fd = open(file.data(), O_RDWR | O_CREAT | O_APPEND | O_DIRECT, 0600);

	start_time = system_clock::now();
	for (int i = 0; i < N; i++) {
		int sz = write(fd, buffer, (size_t) blksize);
		if (sz < 0) {
			cerr << "write file through nvmeof failed: " << strerror(errno) << endl;
			return -1;
		}
	}
	total_time += system_clock::now() - start_time;
	printf("total time for appending is %lu milliseconds\n", chrono::duration_cast<chrono::milliseconds>(total_time.time_since_epoch()).count());
	printf("total data written is %u bytes\n", N * 4096);
	printf("throughput for appending (%d bytes) is %f Mb/s\n", 4096, (N * 4096) * 1.0 / 1024 / 1024 / 
		chrono::duration_cast<chrono::microseconds>(total_time.time_since_epoch()).count() * 1e6);
	printf("IOPS for appending (%d bytes) is %f\n", 4096, N * 1.0 / chrono::duration_cast<chrono::microseconds>(total_time.time_since_epoch()).count() * 1e6);	


	// =================== append many small files ======================
	
}