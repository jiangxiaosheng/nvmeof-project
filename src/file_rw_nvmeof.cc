#include <iostream>
#include <chrono>
#include <string>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <liburing.h>
#include <sys/utsname.h>
#include <thread>
#include <libaio.h>

#include "third_party/argparse.hpp"

using namespace std;
using namespace chrono;

int N;
size_t buffer_size;
size_t blk_size;
int queue_depth;
int32_t idle_threshold;
bool large;
bool poll;
bool closed_loop;
bool test_read;
bool aio;
bool exprm;
int fn;

void test_large_io_uring_poll() {
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
	fds[0] = open("/mnt/large-file", O_WRONLY | O_APPEND | O_CREAT | O_TRUNC | O_DIRECT, 0644);
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
	ret = posix_memalign(&buff, blk_size, buffer_size);
	if (ret) {
		perror("posix_memalign");
		return;
	}
	memset(buff, -1, buffer_size);

	// ================ start a thread to poll cqe ===================
	thread cq_thread([](struct io_uring *ring) {
		int ret;
		for (int i = 0; i < N; i++) {
			struct io_uring_cqe *cqe;
			ret = io_uring_wait_cqe(ring, &cqe);
			if (ret < 0) {
				fprintf(stderr, "Error waiting for completion: %s\n", strerror(-ret));
				return;
			}
			if (cqe->res < 0) {
				fprintf(stderr, "Error in async operation: %s\n", strerror(-cqe->res));
				return;
			}
			io_uring_cqe_seen(ring, cqe);
		}
	}, &ring);

	// ============== io_uring write =======================
	int cnt = 0;
	auto start = system_clock::now();
	for (int i = 0; i < N; i++) {
		ret = io_uring_sqring_wait(&ring);
		if (ret) {
			perror("io_uring_sqring_wait");
			return;
		}
		auto sqe = io_uring_get_sqe(&ring);
		while (!sqe) {
			sqe = io_uring_get_sqe(&ring);
		}
	
		io_uring_prep_write(sqe, 0, buff, buffer_size, 0);
		sqe->flags |= IOSQE_FIXED_FILE;
		io_uring_submit(&ring);
	}

	cq_thread.join();
	auto duration = system_clock::now() - start;
	printf("total time for appending is %lu milliseconds\n", chrono::duration_cast<chrono::milliseconds>(duration).count());
  printf("total data written is %lu bytes\n", N * buffer_size);
  printf("throughput for appending (%ld bytes) is %f MB/s\n", buffer_size, (N * buffer_size) * 1.0 / 1024 / 1024 /
  	chrono::duration_cast<chrono::microseconds>(duration).count() * 1e6);
  printf("IOPS for appending (%ld bytes) is %f\n", buffer_size, N * 1.0 / chrono::duration_cast<chrono::microseconds>(duration).count() * 1e6);
	
	close(fds[0]);
	io_uring_queue_exit(&ring);
}

void test_large_io_uring() {
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

  // ======= open file ==============
  int fd;
	fd = open("/mnt/large-file", O_WRONLY | O_APPEND | O_CREAT | O_TRUNC | O_DIRECT, 0644);
	if (fd < 0) {
		fprintf(stderr, "Error open file: %s", strerror(-ret));
		return;
	}

	// ============= prepare buffers to be written ================
	void *buff;
	ret = posix_memalign(&buff, blk_size, buffer_size);
	if (ret) {
		perror("posix_memalign");
		return;
	}
	memset(buff, -1, buffer_size);

	// ================ start a thread to poll cqe ===================
	thread cq_thread([](struct io_uring *ring) {
		int ret;
		for (int i = 0; i < N; i++) {
			struct io_uring_cqe *cqe;
			ret = io_uring_wait_cqe(ring, &cqe);
			if (ret < 0) {
				fprintf(stderr, "Error waiting for completion: %s\n", strerror(-ret));
				return;
			}
			if (cqe->res < 0) {
				fprintf(stderr, "Error in async operation: %s\n", strerror(-cqe->res));
			}
			io_uring_cqe_seen(ring, cqe);
		}
	}, &ring);

	// ============== io_uring write =======================
	auto start = system_clock::now();
	for (int i = 0; i < N; i++) {
		auto sqe = io_uring_get_sqe(&ring);
		if (!sqe) {
			fprintf(stderr, "Could not get SQE: %d\n", i);
			return;
		}
		io_uring_prep_write(sqe, fd, buff, buffer_size, 0);
		io_uring_submit(&ring);
	}

	cq_thread.join();

	auto duration = system_clock::now() - start;
	printf("total time for appending is %lu milliseconds\n", chrono::duration_cast<chrono::milliseconds>(duration).count());
  printf("total data written is %lu bytes\n", N * buffer_size);
  printf("throughput for appending (%ld bytes) is %f MB/s\n", buffer_size, (N * buffer_size) * 1.0 / 1024 / 1024 /
  	chrono::duration_cast<chrono::microseconds>(duration).count() * 1e6);
  printf("IOPS for appending (%ld bytes) is %f\n", buffer_size, N * 1.0 / chrono::duration_cast<chrono::microseconds>(duration).count() * 1e6);
	
	io_uring_queue_exit(&ring);
	close(fd);
}

void test_small_io_uring() {
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

	// ============= prepare buffers to be written ================
	void *buff;
	ret = posix_memalign(&buff, blk_size, buffer_size);
	if (ret) {
		perror("posix_memalign");
		return;
	}
	memset(buff, -1, buffer_size);

	// ================ start a thread to poll cqe ===================
	thread cq_thread([](struct io_uring *ring) {
		int ret;
		for (int i = 0; i < 2*N; i++) {
			struct io_uring_cqe *cqe;
			ret = io_uring_wait_cqe(ring, &cqe);
			if (ret < 0) {
				fprintf(stderr, "Error waiting for completion: %s\n", strerror(-ret));
				return;
			}
			if (cqe->res < 0) {
				fprintf(stderr, "Error in async operation: %s\n", strerror(-cqe->res));
			}
			io_uring_cqe_seen(ring, cqe);
		}
	}, &ring);

	// ================== io_uring write ==========================
	auto start = system_clock::now();
	for (int i = 0; i < N; i++) {
		string name = "/mnt/small-file-" + to_string(i);
		int fd = open(name.c_str(), O_RDWR | O_APPEND | O_CREAT | O_TRUNC | O_DIRECT, 0644);
		if (fd < 0) {
			perror("open");
			return;
		}
		auto sqe = io_uring_get_sqe(&ring);
		while (!sqe) {
			sqe = io_uring_get_sqe(&ring);
		}
		io_uring_prep_write(sqe, fd, buff, buffer_size, 0);
		sqe->flags |= IOSQE_IO_LINK;

		sqe = io_uring_get_sqe(&ring);
		while (!sqe) {
			sqe = io_uring_get_sqe(&ring);
		}
		io_uring_prep_close(sqe, fd);

		io_uring_submit(&ring);
	}
	
	cq_thread.join();

	auto duration = system_clock::now() - start;
	printf("total time for appending is %lu milliseconds\n", chrono::duration_cast<chrono::milliseconds>(duration).count());
  printf("total data written is %lu bytes\n", N * buffer_size);
  printf("throughput for appending (%ld bytes) is %f MB/s\n", buffer_size, (N * buffer_size) * 1.0 / 1024 / 1024 /
  	chrono::duration_cast<chrono::microseconds>(duration).count() * 1e6);
  printf("IOPS for appending (%ld bytes) is %f\n", buffer_size, N * 1.0 / chrono::duration_cast<chrono::microseconds>(duration).count() * 1e6);

	io_uring_queue_exit(&ring);
}

void test_large_blocking() {
	int fd = open("/mnt/large-file", O_WRONLY | O_APPEND | O_CREAT | O_TRUNC | O_DIRECT, 0644);
	if (fd < 0) {
		perror("open");
		return;
	}

	int ret;

	void *buff;
	ret = posix_memalign(&buff, blk_size, buffer_size);
	if (ret) {
		perror("posix_memalign");
		return;
	}
	memset(buff, -1, buffer_size);

	auto start = system_clock::now();
	for (int i = 0; i < N; i++) {
		ret = write(fd, buff, buffer_size);
		if (ret < 0) {
			perror("write");
			return;
		}
	}

	auto duration = system_clock::now() - start;
	printf("total time for appending is %lu milliseconds\n", chrono::duration_cast<chrono::milliseconds>(duration).count());
  printf("total data written is %lu bytes\n", N * buffer_size);
  printf("throughput for appending (%ld bytes) is %f MB/s\n", buffer_size, (N * buffer_size) * 1.0 / 1024 / 1024 /
  	chrono::duration_cast<chrono::microseconds>(duration).count() * 1e6);
  printf("IOPS for appending (%ld bytes) is %f\n", buffer_size, N * 1.0 / chrono::duration_cast<chrono::microseconds>(duration).count() * 1e6);

	close(fd);
}

void test_small_blocking() {
	int ret;

	void *buff;
	ret = posix_memalign(&buff, blk_size, buffer_size);
	if (ret) {
		perror("posix_memalign");
		return;
	}
	memset(buff, -1, buffer_size);

	auto start = system_clock::now();
	for (int i = 0; i < N; i++) {
		string name = "/mnt/small-file-" + to_string(i);
		int fd = open(name.c_str(), O_RDWR | O_APPEND | O_CREAT | O_TRUNC | O_DIRECT, 0644);
		if (fd < 0) {
			perror("open");
			return;
		}
		ret = write(fd, buff, buffer_size);
		if (ret < 0) {
			perror("write");
			return;
		}
		ret = close(fd);
		if (ret) {
			perror("close");
			return;
		}
	}

	auto duration = system_clock::now() - start;
	printf("total time for appending is %lu milliseconds\n", chrono::duration_cast<chrono::milliseconds>(duration).count());
  printf("total data written is %lu bytes\n", N * buffer_size);
  printf("throughput for appending (%ld bytes) is %f MB/s\n", buffer_size, (N * buffer_size) * 1.0 / 1024 / 1024 /
  	chrono::duration_cast<chrono::microseconds>(duration).count() * 1e6);
  printf("IOPS for appending (%ld bytes) is %f\n", buffer_size, N * 1.0 / chrono::duration_cast<chrono::microseconds>(duration).count() * 1e6);
}

void test_small_aio() {
  int ret;
  io_context_t io_ctx = nullptr;

  ret = io_setup(10, &io_ctx);
  if (ret) {
    cout << ret << endl;
    perror("io_setup");
    return;
  }

  void *buff;
	ret = posix_memalign(&buff, blk_size, buffer_size);
	if (ret) {
		perror("posix_memalign");
		return;
	}
	memset(buff, -1, buffer_size);

  thread cq_thread([&](){
    int max_nr = queue_depth;
    int min_nr = max_nr / 2;
    int cnt = 0;
    struct timespec timeout;
    timeout.tv_sec = 0;
    timeout.tv_nsec = 10'000'000; // 10ms
    struct io_event events[max_nr];
    while (cnt < N) {
      int n = io_getevents(io_ctx, min_nr, max_nr, events, &timeout);
      for (int i = 0; i < n; i++) {
        close(events[i].obj->aio_fildes);
        delete events[i].obj;
      }
      cnt += n;
    }
  });

  auto start = system_clock::now();
  struct iocb **iocbs = new struct iocb*[queue_depth];
  int cnt = 0;
  while (cnt < N) {
    int n = 0;
    for (; n < queue_depth && cnt < N; n++, cnt++) {
      string name = "/mnt/small-file-" + to_string(cnt);
      int fd = open(name.c_str(), O_WRONLY | O_APPEND | O_CREAT | O_TRUNC | O_DIRECT, 0644);
      if (fd < 0) {
        perror("open");
        return;
      }
      iocbs[n] = new iocb;
      io_prep_pwrite(iocbs[n], fd, (void *)buff, buffer_size, 0);
    }
    ret = io_submit(io_ctx, n, iocbs);
  }

  cq_thread.join();
  io_destroy(io_ctx);

	auto duration = system_clock::now() - start;
	printf("total time for appending is %lu milliseconds\n", chrono::duration_cast<chrono::milliseconds>(duration).count());
  printf("total data written is %lu bytes\n", N * buffer_size);
  printf("throughput for appending (%ld bytes) is %f MB/s\n", buffer_size, (N * buffer_size) * 1.0 / 1024 / 1024 /
  	chrono::duration_cast<chrono::microseconds>(duration).count() * 1e6);
  printf("IOPS for appending (%ld bytes) is %f\n", buffer_size, N * 1.0 / chrono::duration_cast<chrono::microseconds>(duration).count() * 1e6);
}

void test_large_aio() {
  int ret;
  io_context_t io_ctx = nullptr;

  ret = io_setup(10, &io_ctx);
  if (ret) {
    cout << ret << endl;
    perror("io_setup");
    return;
  }

  void *buff;
	ret = posix_memalign(&buff, blk_size, buffer_size);
	if (ret) {
		perror("posix_memalign");
		return;
	}
	memset(buff, -1, buffer_size);

  thread cq_thread([&](){
    int max_nr = queue_depth;
    int min_nr = max_nr / 2;
    int cnt = 0;
    struct timespec timeout;
    timeout.tv_sec = 0;
    timeout.tv_nsec = 10'000'000; // 10ms
    struct io_event events[max_nr];
    while (cnt < N) {
      int n = io_getevents(io_ctx, min_nr, max_nr, events, &timeout);
      for (int i = 0; i < n; i++) {
        delete events[i].obj;
      }
      cnt += n;
    }
  });

  string name = "/mnt/large-file";
  int fd = open(name.c_str(), O_WRONLY | O_APPEND | O_CREAT | O_TRUNC | O_DIRECT, 0644);
  if (fd < 0) {
    perror("open");
    return;
  }

  auto start = system_clock::now();
  struct iocb **iocbs = new struct iocb*[queue_depth];
  int cnt = 0;
  while (cnt < N) {
    int n = 0;
    for (; n < queue_depth && cnt < N; n++, cnt++) {
      iocbs[n] = new iocb;
      io_prep_pwrite(iocbs[n], fd, (void *)buff, buffer_size, 0);
    }
    ret = io_submit(io_ctx, n, iocbs);
  }

  cq_thread.join();
  io_destroy(io_ctx);
  close(fd);

	auto duration = system_clock::now() - start;
	printf("total time for appending is %lu milliseconds\n", chrono::duration_cast<chrono::milliseconds>(duration).count());
  printf("total data written is %lu bytes\n", N * buffer_size);
  printf("throughput for appending (%ld bytes) is %f MB/s\n", buffer_size, (N * buffer_size) * 1.0 / 1024 / 1024 /
  	chrono::duration_cast<chrono::microseconds>(duration).count() * 1e6);
  printf("IOPS for appending (%ld bytes) is %f\n", buffer_size, N * 1.0 / chrono::duration_cast<chrono::microseconds>(duration).count() * 1e6);
}

void test_io_uring_exp_contention() {
	int ret;

  struct io_uring ring;
  struct io_uring_params params;

  memset(&params, 0, sizeof(params));

	ret = io_uring_queue_init_params(queue_depth, &ring, &params);
	if (ret) {
			fprintf(stderr, "Unable to setup io_uring: %s\n", strerror(-ret));
			return;
	}

	void *buff;
	ret = posix_memalign(&buff, blk_size, buffer_size);
	if (ret) {
		perror("posix_memalign");
		return;
	}
	memset(buff, -1, buffer_size);

	thread cq_thread([](struct io_uring *ring) {
		int ret;
		for (int i = 0; i < N; i++) {
			struct io_uring_cqe *cqe;
			ret = io_uring_wait_cqe(ring, &cqe);
			if (ret < 0) {
				fprintf(stderr, "Error waiting for completion: %s\n", strerror(-ret));
				return;
			}
			if (cqe->res < 0) {
				fprintf(stderr, "Error in async operation: %s\n", strerror(-cqe->res));
			}
			io_uring_cqe_seen(ring, cqe);
		}
	}, &ring);

  int n_files = fn;
  int fds[n_files];
  for (int i = 0; i < n_files; i++) {
    string name = "/mnt/large-file-" + to_string(i);
    fds[i] = open(name.c_str(), O_WRONLY | O_APPEND | O_CREAT | O_TRUNC | O_DIRECT, 0644);
    if (fds[i] < 0) {
      perror("open");
      return;
    }
  }

	auto start = system_clock::now();
	for (int i = 0; i < N; i++) {
		auto sqe = io_uring_get_sqe(&ring);
		while (!sqe) {
			sqe = io_uring_get_sqe(&ring);
		}
		io_uring_prep_write(sqe, fds[i % n_files], buff, buffer_size, 0);
		io_uring_submit(&ring);
	}
	
	cq_thread.join();

	auto duration = system_clock::now() - start;
	printf("total time for appending is %lu milliseconds\n", chrono::duration_cast<chrono::milliseconds>(duration).count());
  printf("total data written is %lu bytes\n", N * buffer_size);
  printf("throughput for appending (%ld bytes) is %f MB/s\n", buffer_size, (N * buffer_size) * 1.0 / 1024 / 1024 /
  	chrono::duration_cast<chrono::microseconds>(duration).count() * 1e6);
  printf("IOPS for appending (%ld bytes) is %f\n", buffer_size, N * 1.0 / chrono::duration_cast<chrono::microseconds>(duration).count() * 1e6);

	io_uring_queue_exit(&ring);

}

void parse_args(int argc, char **argv) {
	argparse::ArgumentParser program;
	program.add_argument("-d").help("io_uring queue depth").default_value(128).scan<'i', int>();
	program.add_argument("-b").help("the buffer size for each read/write").default_value(4096).scan<'i', int>();
	program.add_argument("-bs").help("the block size specified by the file system").default_value(4096).scan<'i', int>();
	program.add_argument("-n").help("the times we repeat reading/writing").default_value(10'000).scan<'i', int>();
	program.add_argument("-t").help("the threshold (ms) for io_uring to re-enter an sqe in poll mode").default_value(2000).scan<'i', int>();
	program.add_argument("-l").help("append to/read from one large file").default_value(false).implicit_value(true);
	program.add_argument("-p").help("use io_uring poll mode").default_value(false).implicit_value(true);
	program.add_argument("-c").help("test with closed loop (normal blocking read/write syscall rather than io_uring)").default_value(false).implicit_value(true);
	program.add_argument("-r").help("test reading, default is writing").default_value(false).implicit_value(true);
	program.add_argument("-a").help("use libaio instead of io_uring").default_value(false).implicit_value(true);
  program.add_argument("-exp").help("do experiment to help understand results").default_value(false).implicit_value(true);
  program.add_argument("-fn").help("number of large files").default_value(1).scan<'i', int>();
  program.parse_args(argc, argv);

	queue_depth = program.get<int>("-d");
	buffer_size = program.get<int>("-b");
	blk_size = program.get<int>("-bs");
	N = program.get<int>("-n");
	idle_threshold = program.get<int>("-t");
	large = program.get<bool>("-l");
	poll = program.get<bool>("-p");
	closed_loop = program.get<bool>("-c");
	test_read = program.get<bool>("-r");
  aio = program.get<bool>("-a");
  exprm = program.get<bool>("-exp");
  fn = program.get<int>("-fn");
}

int main(int argc, char **argv) {
	parse_args(argc, argv);

  if (exprm) {
    test_io_uring_exp_contention();
    return 0;
  }

	if (closed_loop) {
		if (large) {
			cout << "append to one large file (blocking)\n" << endl;
			test_large_blocking();
		} else {
			cout << "append to " << N << " small files (blocking)\n" << endl;
			test_small_blocking();
		}
	} else {
		if (large) {
      if (aio) {
        cout << "append to one large file (async, libaio)\n" << endl;
        test_large_aio();
      } else {
        if (poll) {
          cout << "append to one large file (async), io_uring poll mode\n" << endl;
          test_large_io_uring_poll();
        } else {
          cout << "append to one large file (async)\n" << endl;
          test_large_io_uring();
        }
      }
		} else {
      if (aio) {
        cout << "append to " << N << " small files (async, libaio)\n" << endl;
        test_small_aio();
      } else {
        if (poll) {
          cerr << "not implemented" << endl;
        } else {
          cout << "append to " << N << " small files (async)\n" << endl;
          test_small_io_uring();
        }
      }
		}
	}

}