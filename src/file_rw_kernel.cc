#include <iostream>
#include <chrono>
#include <string>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <liburing.h>
#include <sys/utsname.h>
#include <thread>

using namespace std;
using namespace chrono;

void test_large_io_uring_poll() {
  // ======= parameters =========
  const int N = 10; // write N times
  size_t buff_size = 4096;
  size_t blk_size = 4096;	// file-system specific
  int queue_depth = 128;
  int32_t idle_threshold = 2000;

  // =============== setup io_uring context =================
	int ret;

  struct io_uring ring;
  struct io_uring_params params;

  memset(&params, 0, sizeof(params));
  params.flags |= IORING_SETUP_SQPOLL;
  params.sq_thread_idle = idle_threshold;

	ret = io_uring_queue_init_params(queue_depth, &ring, &params);
	if (ret) {
			fprintf(stderr, "Unable to setup io_uring: %s\n", strerror(-ret));
			return;
	}

  // ======= open file first and then register them ==============
  int fds[1];
	fds[0] = open("large-file", O_RDWR | O_APPEND | O_CREAT , 0644);
	if (fds[0] < 0) {
		fprintf(stderr, "Error open file: %s", strerror(-ret));
		return;
	}

	ret = io_uring_register_files(&ring, fds, 1);
	if (ret) {
		fprintf(stderr, "Error registering files: %s", strerror(-ret));
		return;
	}

	// ============= prepare buffers to be written ================
	void *buff;
	ret = posix_memalign(&buff, blk_size, buff_size);
	if (ret) {
		perror("posix_memalign");
		return;
	}

	// ================ start a thread to poll cqe ===================
	thread cq_thread([&]() {
		for (int i = 0; i < N; i++) {
			struct io_uring_cqe *cqe;
			io_uring_wait_cqe(&ring, &cqe);
			if (ret < 0) {
				fprintf(stderr, "Error waiting for completion: %s\n", strerror(-ret));
				return;
			}
			if (cqe->res < 0) {
				fprintf(stderr, "Error in async operation: %s\n", strerror(-cqe->res));
			}
			io_uring_cqe_seen(&ring, cqe);
		}
	});

	// ============== io_uring write =======================
	for (int i = 0; i < N; i++) {
		auto sqe = io_uring_get_sqe(&ring);
		while (!sqe) {
			// sqe = io_uring_get_sqe(&ring);
			fprintf(stderr, "Could not get SQE: %d\n", i);
			return;
		}
		sqe->flags |= IOSQE_FIXED_FILE | IOSQE_IO_LINK;
		io_uring_prep_write(sqe, 0, buff, buff_size, 0);
		io_uring_submit(&ring);
	}

	// ================ read from completion queue ================

	cq_thread.join();
	
}

void test_large_io_uring() {
	  // ======= parameters =========
  const int N = 10; // write N times
  size_t buff_size = 4096;
  size_t blk_size = 4096;	// file-system specific
  int queue_depth = 128;

  // =============== setup io_uring context =================
	int ret;

  struct io_uring ring;
  struct io_uring_params params;

  memset(&params, 0, sizeof(params));

	ret = io_uring_queue_init_params(queue_depth, &ring, &params);
	if (ret) {
			fprintf(stderr, "Unable to setup io_uring: %s\n", strerror(-ret));
			return;
	}

  // ======= open file first and then register them ==============
  int fd;
	fd = open("large-file", O_RDWR | O_APPEND | O_CREAT , 0644);
	if (fd < 0) {
		fprintf(stderr, "Error open file: %s", strerror(-ret));
		return;
	}

	// ============= prepare buffers to be written ================
	void *buff;
	ret = posix_memalign(&buff, blk_size, buff_size);
	if (ret) {
		perror("posix_memalign");
		return;
	}

	// ================ start a thread to poll cqe ===================
	// thread cq_thread([](struct io_uring *ring) {
	// 	int ret;
	// 	for (int i = 0; i < N; i++) {
	// 		struct io_uring_cqe *cqe;
	// 		ret = io_uring_wait_cqe(ring, &cqe);
	// 		if (ret < 0) {
	// 			fprintf(stderr, "Error waiting for completion: %s\n", strerror(-ret));
	// 			return;
	// 		}
	// 		if (cqe->res < 0) {
	// 			fprintf(stderr, "Error in async operation: %s\n", strerror(-cqe->res));
	// 		}
	// 		io_uring_cqe_seen(ring, cqe);
	// 	}
	// }, &ring);

	// ============== io_uring write =======================
	for (int i = 0; i < N; i++) {
		auto sqe = io_uring_get_sqe(&ring);
		while (!sqe) {
			// sqe = io_uring_get_sqe(&ring);
			fprintf(stderr, "Could not get SQE: %d\n", i);
			return;
		}
		// sqe->flags |= IOSQE_IO_LINK;
		io_uring_prep_write(sqe, fd, buff, buff_size, 0);
		io_uring_submit(&ring);
	}

	// ================ read from completion queue ================

	// cq_thread.join();

	for (int i = 0; i < N; i++) {
		struct io_uring_cqe *cqe;
		ret = io_uring_wait_cqe(&ring, &cqe);
		if (ret < 0) {
			fprintf(stderr, "Error waiting for completion: %s\n", strerror(-ret));
			return;
		}
		if (cqe->res < 0) {
			fprintf(stderr, "Error in async operation: %s\n", strerror(-cqe->res));
		}
		io_uring_cqe_seen(&ring, cqe);
	}
}


int main(int argc, char **argv) {

  test_large_io_uring();
  // string ss[10000];
  // int blksize = 4096;
  // int align = blksize - 1;
  // char *buffer = new char[blksize + align];
  // buffer = (char *) (((uintptr_t) buffer + align) &~ ((uintptr_t) align));

  // int N = 20000;

  // decltype(system_clock::now()) start_time, end_time, total_time;

  // ================ append one large file ====================
  // string file = "/mnt/nvmeof-largefile";
  // int fd = open(file.data(), O_RDWR | O_CREAT | O_APPEND | O_DIRECT, 0600);
  // if (fd < 0) {
  // 	perror("open filed failed");
  // 	return -1;
  // }

  // start_time = system_clock::now();
  // for (int i = 0; i < N; i++) {
  // 	int sz = write(fd, buffer, (size_t) blksize);
  // 	if (sz < 0) {
  // 		cerr << "write file through nvmeof failed: " << strerror(errno) << endl;
  // 		return -1;
  // 	}
  // }
  // total_time += system_clock::now() - start_time;
  // printf("total time for appending is %lu milliseconds\n", chrono::duration_cast<chrono::milliseconds>(total_time.time_since_epoch()).count());
  // printf("total data written is %u bytes\n", N * 4096);
  // printf("throughput for appending (%d bytes) is %f Mb/s\n", 4096, (N * 4096) * 1.0 / 1024 / 1024 /
  // 	chrono::duration_cast<chrono::microseconds>(total_time.time_since_epoch()).count() * 1e6);
  // printf("IOPS for appending (%d bytes) is %f\n", 4096, N * 1.0 / chrono::duration_cast<chrono::microseconds>(total_time.time_since_epoch()).count() * 1e6);


  // =================== append many small files ======================
  // start_time = system_clock::now();
  // total_time = {};
  // for (int i = 0; i < N; i++) {
  // 	string file = "/mnt/small-file" + to_string(i);
  // 	int fd = open(file.data(), O_RDWR | O_CREAT | O_APPEND | O_DIRECT, 0600);
  // 	if (fd < 0) {
  // 		perror("open filed failed");
  // 		return -1;
  // 	}
  // 	int sz = write(fd, buffer, (size_t) blksize);
  // 	if (sz < 0) {
  // 		cerr << "write file through nvmeof failed: " << strerror(errno) << endl;
  // 		return -1;
  // 	}
  // }
  // total_time += system_clock::now() - start_time;
  // printf("total time for appending is %lu milliseconds\n", chrono::duration_cast<chrono::milliseconds>(total_time.time_since_epoch()).count());
  // printf("total data written is %u bytes\n", N * 4096);
  // printf("throughput for appending (%d bytes) is %f Mb/s\n", 4096, (N * 4096) * 1.0 / 1024 / 1024 /
  // 	chrono::duration_cast<chrono::microseconds>(total_time.time_since_epoch()).count() * 1e6);
  // printf("IOPS for appending (%d bytes) is %f\n", 4096, N * 1.0 / chrono::duration_cast<chrono::microseconds>(total_time.time_since_epoch()).count() * 1e6);

}