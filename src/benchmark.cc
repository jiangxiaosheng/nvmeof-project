#include <pthread.h>
#include <liburing.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string>
#include <sys/types.h>
#include <fcntl.h>
#include <string.h>
#include <malloc.h>
#include <chrono>

#include "third_party/argparse.hpp"

const unsigned KB = 1024;
const unsigned MB = 1024 * 1024;
const unsigned long GB = 1024 * 1024 * 1024;
const unsigned PAGESIZE = 4096;

using namespace std::chrono;

/* global options */
int num_workers;
unsigned bs; /* in KB */
unsigned io_uring_batch_submit;
unsigned rdma_batch_submit;
std::string test_file;
unsigned file_size; /* in GB */
int io_uring_queue_depth;
int rdma_queue_depth;
unsigned runtime; /* in seconds */
bool client;

struct io_worker {
	struct io_uring ring;
	struct io_uring_params params;
	unsigned sqes_queued;
	unsigned long cur_offset;
	unsigned long io_completed;
	unsigned long free_io_units;
	pthread_t tid;
	cpu_set_t cpuset;
	int fd;
	bool stopped;
	decltype(system_clock::now()) start_time;
};


/* set the worker thread's cpu affinity */
int set_worker_cpu(struct io_worker *worker, int cpu) {
	CPU_ZERO(&worker->cpuset);
	CPU_SET(cpu, &worker->cpuset);

	if (pthread_setaffinity_np(worker->tid, sizeof(worker->cpuset), &worker->cpuset)) {
		perror("set cpu affinity error");
		return 1;
	}

	return 0;
}

inline bool runtime_exceeded(decltype(system_clock::now()) start) {
	auto now = system_clock::now();
	return duration_cast<seconds>(now - start).count() > runtime;
}

int reap_io_uring_cq(struct io_worker *worker, int min) {
	struct io_uring_cqe *cqe;
	unsigned head;
	unsigned reaped = 0;

	do {
		io_uring_for_each_cqe(&worker->ring, head, cqe) {
			if (cqe->res < 0) {
				printf("fio: io_uring cqe error: %s\n", strerror(-cqe->res));
				return -1;
			}
			io_uring_cqe_seen(&worker->ring, cqe);
			reaped++;
			worker->io_completed++;
			worker->free_io_units++;
		}
	} while (reaped < min);

	return reaped;
}


/* actual I/Os executed by a worker */
void *do_io(void *arg) {
	struct io_worker *worker = (struct io_worker *) arg;
	struct stat st;
	void *data_buffer;
	decltype(system_clock::now()) start;
	int ret;
	
	worker->fd = open(test_file.data(), O_DIRECT | O_RDWR | O_CREAT, 0644);
	if (worker->fd < 0) {
		perror("open");
		goto err;
	}

	if (fstat(worker->fd, &st)) {
		perror("fstat");
		goto err;
	}

	if (st.st_size != file_size * GB) {
		printf("resizing test file %s, original size is %luB, setting to %luB\n", test_file.data(), st.st_size, file_size * GB);
		if (fallocate(worker->fd, 0, 0, file_size * GB)) {
			perror("fallocate");
			goto err;
		}
	}

	memset(&worker->params, 0, sizeof(worker->params));
	if (io_uring_queue_init_params(io_uring_queue_depth, &worker->ring, &worker->params)) {
		perror("io_uring_queue_init_params");
		goto err;
	}

	data_buffer = memalign(PAGESIZE, bs);
	if (!data_buffer) {
		perror("memalign");
		goto err;
	}

	worker->start_time = system_clock::now();
	while (!runtime_exceeded(worker->start_time)) {
		struct io_uring_sqe *sqe;
		
		do {
			sqe = io_uring_get_sqe(&worker->ring);
			if (sqe != NULL)
				break;
			reap_io_uring_cq(worker, 0);
		} while (true);
		io_uring_prep_write(sqe, worker->fd, data_buffer, bs * KB, worker->cur_offset);
		// printf("fd %d, data_buffer %p, bs %d, offset %lu\n", worker->fd, data_buffer, bs, worker->cur_offset);
		worker->cur_offset += bs * KB;
		if (worker->cur_offset >= file_size * GB)
			worker->cur_offset = 0;
		worker->free_io_units--;

		worker->sqes_queued++;
		if (worker->sqes_queued == io_uring_batch_submit) {
			while (worker->sqes_queued != 0) {
				ret = io_uring_submit(&worker->ring);
				if (ret > 0) {
					worker->sqes_queued -= ret;
				} else {
					printf("im busy\n"); // debugging
					reap_io_uring_cq(worker, 0);
				}
			}
		}

		if (worker->free_io_units == 0) {
			reap_io_uring_cq(worker, 1);
		}
	}
	
	worker->stopped = true;
	return NULL;

err:
	pthread_exit((void *)1);
	return NULL;
}

/* create a worker thread on the specified cpu */
struct io_worker *launch_worker(int cpu) {
	struct io_worker *worker = (struct io_worker *) malloc(sizeof(struct io_worker));

	memset(worker, 0, sizeof(worker));
	worker->free_io_units = io_uring_queue_depth;
	worker->stopped = false;

	pthread_create(&worker->tid, NULL, do_io, (void *) worker);
	if (set_worker_cpu(worker, cpu)) {
		free(worker);
		return NULL;
	}

	return worker;
}

/* no need to prevent race condition as the accurate numer will be calculated when it terminates */
void *calculate_stats(void *arg) {
	struct io_worker *worker = (struct io_worker *) (arg);
	unsigned io_completed;
	double thru;
	unsigned kiops;
	unsigned duration_ms;

	printf("bs=%dKB, io_uring_queue_depth=%d, test_file=%s, io_uring_batch_submit=%d\n\n",
		bs, io_uring_queue_depth, test_file.data(), io_uring_batch_submit);

	while (!worker->stopped) {
		io_completed = worker->io_completed;
		duration_ms = duration_cast<milliseconds>(system_clock::now() - worker->start_time).count();
		if (io_completed == 0 || duration_ms == 0)
			continue;

		// printf("io_completed %lu, duration_ms %d\n", io_completed, duration_ms);
		thru = 1.0 * io_completed * bs * KB / MB / duration_ms * 1000;
		kiops = io_completed / duration_ms;
		printf("\rthroughput: %f MB/s (%dK IOPS)\n", thru, kiops);
		fflush(stdout);
		sleep(1);
	}

	return NULL;
}


void parse_args(int argc, char *argv[]) {
	argparse::ArgumentParser program;
	program.add_argument("-bs").help("block size for each I/O in KB").default_value(4).scan<'i', int>();
	program.add_argument("-file").help("test file on which we do the I/Os").default_value("");
	program.add_argument("-iouring-qd").help("io_uring queue sq depth").default_value(128).scan<'i', int>();
	program.add_argument("-runtime").help("runtime of the benchmark").default_value(30).scan<'i', int>();
	program.add_argument("-filesize").help("size of the test file in GB").default_value(10).scan<'i', int>();
	program.add_argument("-iouring-batch-submit").help("io_uring sqe submission batch").default_value(32).scan<'i', int>();
	program.add_argument("-client").help("rdma io_uring client").default_value(false).implicit_value(true);
	program.parse_args(argc, argv);

	bs = program.get<int>("-bs");
	test_file = program.get<std::string>("-file");
	file_size = program.get<int>("-filesize");
	io_uring_queue_depth = program.get<int>("-qd");
	runtime = program.get<int>("-runtime");
	io_uring_batch_submit = program.get<int>("-batch-submit");
	client = program.get<bool>("-client");

	if (test_file == "") {
		printf("no test file given");
	}
}


int main(int argc, char *argv[]) {
	struct io_worker *worker;
	pthread_t monitor_thread;

	parse_args(argc, argv);
	
	worker = launch_worker(0);
	monitor_thread = pthread_create(&monitor_thread, NULL, calculate_stats, (void *) worker);

	pthread_join(worker->tid, NULL);
	pthread_join(monitor_thread, NULL);
}