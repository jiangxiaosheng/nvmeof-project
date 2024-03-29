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
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>

#include "third_party/argparse.hpp"

const unsigned KB = 1024;
const unsigned MB = 1024 * 1024;
const unsigned long GB = 1024 * 1024 * 1024;
const unsigned PAGESIZE = 4096;
const int POLL_BATCH = 16;
const int RDMA_REAP_FREQ = 16;

using namespace std::chrono;

/* global options */
int num_workers;
unsigned long bs;
unsigned io_uring_batch_submit;
unsigned rdma_batch_submit;
std::string test_file;
unsigned long file_size;
int io_uring_queue_depth;
int rdma_queue_depth;
unsigned runtime; /* in seconds */
bool client;
bool user_rdma;
std::string host;
int base_port;
bool event;

struct rdma_rmt_buf {
	uint64_t addr;
	uint32_t size;
	uint32_t rkey;
};

struct io_worker {
	int worker_id;
	struct io_uring ring;
	struct io_uring_params params;
	unsigned io_uring_sqe_queued;
	unsigned long rdma_wr_posted;
	unsigned long cur_offset;
	unsigned long io_completed; // for io_uring only
	unsigned long free_io_units_io_uring;
	unsigned long free_io_units_rdma;
	pthread_t tid;
	cpu_set_t cpuset;
	int fd;
	bool stopped;
	decltype(system_clock::now()) start_time;

	/* rdma stuff */
	struct rdma_cm_id *cm_id, *cm_listen_id;
	struct ibv_comp_channel *comp_channel;
	struct rdma_event_channel *cm_channel;
	struct ibv_cq *cq;
	struct ibv_pd *pd;
	struct ibv_qp *qp;
	struct rdma_rmt_buf rmt_buf;
	uint64_t data_pool;
	uint32_t data_pool_size;
	struct ibv_mr *data_pool_mr;
	struct sockaddr_in addr;
	int port;
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
				printf("io_uring cqe error: %s\n", strerror(-cqe->res));
				return -1;
			}
			io_uring_cqe_seen(&worker->ring, cqe);
			reaped++;
			worker->io_completed++;
			worker->free_io_units_io_uring++;
		}
	} while (reaped < min);

	return reaped;
}


/* actual I/Os executed by a worker */
void *do_io_io_uring(void *arg) {
	struct io_worker *worker = (struct io_worker *) arg;
	struct stat st;
	void *data_buffer;
	decltype(system_clock::now()) start;
	struct iovec iovecs;
	int ret;
	
	worker->fd = open(test_file.data(), O_DIRECT | O_WRONLY | O_CREAT, 0644);
	if (worker->fd < 0) {
		perror("open");
		goto err;
	}

	if (fstat(worker->fd, &st)) {
		perror("fstat");
		goto err;
	}

	if (st.st_size != file_size) {
		printf("resizing test file %s, original size is %luB, setting to %luB\n", test_file.data(), st.st_size, file_size);
		if (fallocate(worker->fd, 0, 0, file_size)) {
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
			if (reap_io_uring_cq(worker, 0) < 0)
				goto err;
		} while (true);
		iovecs.iov_len = bs;
		iovecs.iov_base = data_buffer;
		io_uring_prep_writev(sqe, worker->fd, &iovecs, 1, worker->cur_offset);
		// printf("fd %d, data_buffer %p, bs %d, offset %lu\n", worker->fd, data_buffer, bs, worker->cur_offset);
		worker->cur_offset += bs;
		if (worker->cur_offset + bs >= file_size)
			worker->cur_offset = 0;
		worker->free_io_units_io_uring--;

		worker->io_uring_sqe_queued++;
		if (worker->io_uring_sqe_queued == io_uring_batch_submit) {
			while (worker->io_uring_sqe_queued != 0) {
				ret = io_uring_submit(&worker->ring);
				if (ret > 0) {
					worker->io_uring_sqe_queued -= ret;
				} else {
					printf("im busy\n"); // debugging
					if (reap_io_uring_cq(worker, 0) < 0)
						goto err;
				}
			}
		}

		if (worker->free_io_units_io_uring == 0) {
			if (reap_io_uring_cq(worker, 1) < 0)
				goto err;
			// printf("free io units: %d\n", worker->free_io_units_io_uring);
		}
	}
	
	worker->stopped = true;
	return NULL;

err:
	pthread_exit((void *)1);
	return NULL;
}

int get_next_channel_event(struct io_worker *worker,
						enum rdma_cm_event_type wait_event) {
	struct rdma_cm_event *event;
	
	if (rdma_get_cm_event(worker->cm_channel, &event)) {
		perror("rdma_get_cm_event");
		return 1;
	}

	if (event->event != wait_event) {
		printf("rdma event type mismatch! event is %s instead of %s\n",
			rdma_event_str(event->event), rdma_event_str(wait_event));
		return 1;
	}

	switch (event->event) {
	case RDMA_CM_EVENT_CONNECT_REQUEST:
		worker->cm_id = event->id;
		break;
	default:
		break;
	}

	rdma_ack_cm_event(event);
	return 0;
}

int rdma_handshake(struct io_worker *worker) {
	struct rdma_conn_param conn_param;
	struct ibv_device_attr dev_attr;
	int ret;

	if (ibv_query_device(worker->cm_id->verbs, &dev_attr)) {
		perror("ibv_query_device");
		return 1;
	}

	memset(&conn_param, 0, sizeof(conn_param));
	conn_param.responder_resources = dev_attr.max_qp_rd_atom;
	conn_param.initiator_depth = dev_attr.max_qp_rd_atom;
	conn_param.retry_count = 7;
	conn_param.rnr_retry_count = 7;


	if (client) {
		struct ibv_recv_wr token_wr, *bad_wr;
		struct ibv_sge sgl;
		struct ibv_cq *ev_cq;
		void *ev_ctx;
		struct ibv_wc wc;
		struct ibv_mr *tmp_mr;
		

		if (rdma_connect(worker->cm_id, &conn_param)) {
			perror("rdma_connect");
			return 1;
		}

		if (get_next_channel_event(worker, RDMA_CM_EVENT_ESTABLISHED))
			return 1;

		tmp_mr = ibv_reg_mr(worker->pd, (void *) &worker->rmt_buf, sizeof(worker->rmt_buf),
						IBV_ACCESS_LOCAL_WRITE);
		if (!tmp_mr) {
			perror("ibv_reg_mr");
			return 1;
		}

		sgl.addr = (uint64_t) &worker->rmt_buf;
		sgl.length = sizeof(worker->rmt_buf);
		sgl.lkey = tmp_mr->lkey;

		token_wr.sg_list = &sgl;
		token_wr.num_sge = 1;
		token_wr.next = NULL;

		if (ibv_post_recv(worker->qp, &token_wr, &bad_wr)) {
			perror("ibv_post_recv");
			return 1;
		}

		if (ibv_get_cq_event(worker->comp_channel, &ev_cq, &ev_ctx)) {
			perror("ibv_get_cq_event");
			return 1;
		}

		if (ev_cq != worker->cq) {
			printf("unknown cq\n");
			return 1;
		}

		if (ibv_req_notify_cq(worker->cq, 0)) {
			perror("ibv_req_notify_cq");
			return 1;
		}

		do {
			ret = ibv_poll_cq(worker->cq, 1, &wc);
		} while (ret < 1);
		if (wc.status) {
			printf("cq completion status: %s\n", ibv_wc_status_str(wc.status));
			return 1;
		}

		ibv_ack_cq_events(worker->cq, 1);

		worker->data_pool = (uint64_t) (unsigned long) memalign(PAGESIZE, worker->rmt_buf.size);
		if (!worker->data_pool) {
			perror("memalign");
			return 1;
		}
		worker->data_pool_mr = ibv_reg_mr(worker->pd, (void *) worker->data_pool, worker->rmt_buf.size, 
					IBV_ACCESS_LOCAL_WRITE);
		if (!worker->data_pool_mr) {
			perror("ibv_reg_mr");
			return 1;
		}

	} else {
		struct ibv_send_wr token_wr, *bad_wr;
		struct ibv_sge sgl;
		struct ibv_cq *ev_cq;
		void *ev_ctx;
		struct ibv_wc wc;
		struct ibv_mr *tmp_mr;

		if (rdma_accept(worker->cm_id, &conn_param)) {
			perror("rdma_accept");
			return 1;
		}

		if (get_next_channel_event(worker, RDMA_CM_EVENT_ESTABLISHED))
			return 1;

		usleep(500000);

		worker->data_pool = (uint64_t) memalign(PAGESIZE, worker->data_pool_size);
		if (!worker->data_pool) {
			perror("memalign");
			return 1;
		}
		worker->data_pool_mr = ibv_reg_mr(worker->pd, (void *) worker->data_pool, worker->data_pool_size,
							IBV_ACCESS_LOCAL_WRITE |
							IBV_ACCESS_REMOTE_READ |
							IBV_ACCESS_REMOTE_WRITE);
		if (!worker->data_pool_mr) {
			perror("ibv_reg_mr");
			return 1;
		}

		worker->rmt_buf.addr = worker->data_pool;
		worker->rmt_buf.size = worker->data_pool_size;
		worker->rmt_buf.rkey = worker->data_pool_mr->rkey;
		
		/* allocate a tmp mr for the purpose of token exchange */
		tmp_mr = ibv_reg_mr(worker->pd, &worker->rmt_buf, sizeof(worker->rmt_buf), IBV_ACCESS_LOCAL_WRITE);
		if (!tmp_mr) {
			perror("ibv_reg_mr");
			return 1;
		}

		sgl.addr = (uint64_t) (unsigned long) &worker->rmt_buf;
		sgl.length = sizeof(worker->rmt_buf);
		sgl.lkey = tmp_mr->lkey;
		token_wr.opcode = IBV_WR_SEND;
		token_wr.send_flags = IBV_SEND_SIGNALED;
		token_wr.sg_list = &sgl;
		token_wr.num_sge = 1;
		token_wr.wr_id = 1;
		token_wr.next = NULL;

		if (ibv_post_send(worker->qp, &token_wr, &bad_wr)) {
			perror("ibv_post_send");
			return 1;
		}
		
		if (ibv_get_cq_event(worker->comp_channel, &ev_cq, &ev_ctx)) {
			perror("ibv_get_cq_event");
			return 1;
		}

		if (ev_cq != worker->cq) {
			printf("unknown cq\n");
			return 1;
		}

		if (ibv_req_notify_cq(worker->cq, 0)) {
			perror("ibv_req_notify_cq");
			return 1;
		}

		do {
			ret = ibv_poll_cq(worker->cq, 1, &wc);
		} while (ret < 1);
		if (wc.status) {
			printf("cq completion status: %s\n", ibv_wc_status_str(wc.status));
			return 1;
		}

		ibv_ack_cq_events(worker->cq, 1);

		if (ibv_dereg_mr(tmp_mr)) {
			perror("ibv_dereg_mr");
			return 1;
		}

	}

	return 0;
}

int rdma_setup(struct io_worker *worker) {
	struct ibv_qp_init_attr init_attr;
	int ret;
	
	worker->cm_channel = rdma_create_event_channel();
	if (!worker->cm_channel) {
		perror("rdma_create_event_channel");
		return 1;
	}

	if (client) {
		if (rdma_create_id(worker->cm_channel, &worker->cm_id, worker, RDMA_PS_TCP)) {
			perror("rdma_create_id");
			return 1;
		}
	} else {
		if (rdma_create_id(worker->cm_channel, &worker->cm_listen_id, worker, RDMA_PS_TCP)) {
			perror("rdma_create_id");
			return 1;
		}
	}
	
	worker->addr.sin_family = AF_INET;
	worker->addr.sin_port = htons(base_port + worker->worker_id);
	inet_aton(host.data(), &worker->addr.sin_addr);

	if (client) {
		if (rdma_resolve_addr(worker->cm_id, NULL, (struct sockaddr *) &worker->addr, 2000)) {
			perror("rdma_resolve_addr");
			return 1;
		}

		if (get_next_channel_event(worker, RDMA_CM_EVENT_ADDR_RESOLVED))
			return 1;

		if (rdma_resolve_route(worker->cm_id, 2000)) {
			perror("rdma_resolve_route");
			return 1;
		}

		if (get_next_channel_event(worker, RDMA_CM_EVENT_ROUTE_RESOLVED))
			return 1;

	} else {
		if (rdma_bind_addr(worker->cm_listen_id, (struct sockaddr *) &worker->addr)) {
			perror("rdma_bind_addr");
			return 1;
		}

		if (rdma_listen(worker->cm_listen_id, 3)) {
			perror("rdma_listen");
			return 1;
		}

		if (get_next_channel_event(worker, RDMA_CM_EVENT_CONNECT_REQUEST))
			return 1;

	}

	worker->pd = ibv_alloc_pd(worker->cm_id->verbs);
	if (!worker->pd) {
		perror("ibv_alloc_pd");
		return 1;
	}

	worker->comp_channel = ibv_create_comp_channel(worker->cm_id->verbs);
	if (!worker->comp_channel) {
		perror("ibv_create_comp_channel");
		return 1;
	}

	worker->cq = ibv_create_cq(worker->cm_id->verbs, rdma_queue_depth * 2, worker, worker->comp_channel, 0);
	if (!worker->cq) {
		perror("ibv_create_cq");
		return 1;
	}

	if (ibv_req_notify_cq(worker->cq, 0)) {
		perror("ibv_req_notify_cq");
		return 1;
	}

	memset(&init_attr, 0, sizeof(init_attr));
	init_attr.cap.max_send_wr = rdma_queue_depth;
	init_attr.cap.max_recv_wr = rdma_queue_depth;
	init_attr.cap.max_recv_sge = 1;
	init_attr.cap.max_send_sge = 1;
	init_attr.qp_type = IBV_QPT_RC;
	init_attr.send_cq = worker->cq;
	init_attr.recv_cq = worker->cq;

	if (rdma_create_qp(worker->cm_id, worker->pd, &init_attr)) {
		perror("rdma_create_qp");
		return 1;
	}
	worker->qp = worker->cm_id->qp;
	
	return 0;
}

int reap_rdma_cq(struct io_worker *worker, int min, struct ibv_wc *wc) {
	struct ibv_cq *ev_cq;
	void *ev_ctx;
	unsigned reaped = 0;
	struct io_uring_sqe *sqe;
	struct iovec iovecs;
	int ret;

again:
	if (event) {
		if (ibv_get_cq_event(worker->comp_channel, &ev_cq, &ev_ctx)) {
			perror("ibv_get_cq_event");
			return -1;
		}
		if (ev_cq != worker->cq) {
			printf("unknown cq\n");
			return -1;
		}
		if (ibv_req_notify_cq(worker->cq, 0)) {
			perror("ibv_req_notify_cq");
			return -1;
		}
	}

	do {
		ret = ibv_poll_cq(worker->cq, POLL_BATCH, wc);
		if (ret < 0) {
			perror("ibv_poll_cq");
			return -1;
		} else if (ret == 0) {
			break;
		} else {
			for (int i = 0; i < ret; i++) {
				if (wc[i].status != IBV_WC_SUCCESS) {
					printf("cq completion %d status: %d(%s), %d, %d\n",
						i, wc[i].status, ibv_wc_status_str(wc[i].status), ret, POLL_BATCH);
					return -1;
				}

				do {
					sqe = io_uring_get_sqe(&worker->ring);
					if (sqe != NULL)
						break;
					if (reap_io_uring_cq(worker, 0) < 0)
						return -1;
				} while (true);
				
				iovecs.iov_len = bs;
				iovecs.iov_base = (void *) wc[i].wr_id;
				io_uring_prep_writev(sqe, worker->fd, &iovecs, 1, worker->cur_offset);
				printf("iovec len: %d, iov_base: %p, file cur offset: %lu\n", 
					iovecs.iov_len, iovecs.iov_base, worker->cur_offset);
				worker->cur_offset += bs;
				if (worker->cur_offset + bs >= file_size)
					worker->cur_offset = 0;
				worker->free_io_units_io_uring--;

				worker->io_uring_sqe_queued++;

				if (worker->io_uring_sqe_queued == io_uring_batch_submit) {
					int sqe_submitted;
					while (worker->io_uring_sqe_queued != 0) {
						sqe_submitted = io_uring_submit(&worker->ring);
						if (sqe_submitted > 0) {
							worker->io_uring_sqe_queued -= sqe_submitted;
						} else {
							printf("im busy\n"); // debugging
							if (reap_io_uring_cq(worker, 0) < 0)
								return -1;
						}
					}
				}

				if (worker->free_io_units_io_uring == 0) {
					printf("no free io units, reaping...\n");
					if (reap_io_uring_cq(worker, 1) < 0)
						return -1;
				}
				
			}

			reaped += ret;
			worker->free_io_units_rdma += ret;
		}
	} while (ret);

	if (reaped < min)
		goto again;

	if (event)
		ibv_ack_cq_events(worker->cq, reaped);
	return 0;
}

void rdma_client_terminate(struct io_worker *worker) {
	struct ibv_send_wr end_wr, *bad_wr;
	struct ibv_sge sgl;

	sgl.addr = worker->data_pool;
	sgl.length = 4;
	sgl.lkey = worker->data_pool_mr->lkey;

	end_wr.sg_list = &sgl;
	end_wr.num_sge = 1;
	end_wr.next = NULL;

	if (ibv_post_send(worker->qp, &end_wr, &bad_wr)) {
		perror("ibv_post_send");
	}
}

void *do_io_user_rdma(void *arg) {
	struct io_worker *worker = (struct io_worker *) arg;
	struct ibv_send_wr wr, *bad_wr;
	struct ibv_sge sgl;
	struct ibv_wc wc[POLL_BATCH];
	struct stat st;
	unsigned int offset = 0;

	if (rdma_setup(worker))
		goto err;

	if (rdma_handshake(worker))
		goto err;

	if (!client) {
		struct ibv_recv_wr end_wr, *bad_wr;
		struct ibv_cq *ev_cq;
		void *ev_ctx;
		
		sgl.addr = worker->data_pool;
		sgl.length = 4;
		sgl.lkey = worker->data_pool_mr->lkey;

		end_wr.sg_list = &sgl;
		end_wr.num_sge = 1;
		end_wr.next = NULL;

		ibv_post_recv(worker->qp, &end_wr, &bad_wr);

		if (ibv_get_cq_event(worker->comp_channel, &ev_cq, &ev_ctx)) {
			perror("ibv_get_cq_event");
			goto err_server;
		}
		if (ev_cq != worker->cq) {
			printf("unknown cq\n");
			goto err_server;
		}
		if (ibv_poll_cq(worker->cq, 1, wc) != 1) {
			perror("ibv_poll_cq");
			goto err_server;
		}
		ibv_ack_cq_events(worker->cq, 1);
		
		return NULL;
	err_server:
		pthread_exit((void *)1);
		return NULL;
	}

	/* setup io_uring stuff */
	worker->fd = open(test_file.data(), O_DIRECT | O_WRONLY | O_CREAT, 0644);
	if (worker->fd < 0) {
		perror("open");
		goto err;
	}

	if (fstat(worker->fd, &st)) {
		perror("fstat");
		goto err;
	}

	if (st.st_size != file_size) {
		printf("resizing test file %s, original size is %luB, setting to %luB\n", test_file.data(), st.st_size, file_size);
		if (fallocate(worker->fd, 0, 0, file_size)) {
			perror("fallocate");
			goto err;
		}
	}

	memset(&worker->params, 0, sizeof(worker->params));
	if (io_uring_queue_init_params(io_uring_queue_depth, &worker->ring, &worker->params)) {
		perror("io_uring_queue_init_params");
		goto err;
	}

	/* setup rdma stuff */
	sgl.length = bs;
	sgl.lkey = worker->data_pool_mr->lkey;
	wr.opcode = IBV_WR_RDMA_READ;
	wr.send_flags = IBV_SEND_SIGNALED;
	wr.wr.rdma.rkey = worker->rmt_buf.rkey;
	wr.sg_list = &sgl;
	wr.num_sge = 1;
	wr.next = NULL;

	worker->start_time = system_clock::now();
	// printf("remote data buffer size = %d\n", worker->rmt_buf.size);
	while (!runtime_exceeded(worker->start_time)) {
		int ret;
		sgl.addr = worker->data_pool + offset;
		wr.wr.rdma.remote_addr = worker->rmt_buf.addr + offset;
		wr.wr_id = worker->rmt_buf.addr + offset;
		offset += bs;
		// printf("offset: %d, buf size: %d\n", offset, worker->rmt_buf.size);
		if (offset + bs >= worker->rmt_buf.size)
			offset = 0;
		
		worker->rdma_wr_posted++;
		if ((ret = ibv_post_send(worker->qp, &wr, &bad_wr)) != 0) {
			printf("ibv_post_send: %s, ret = %d, wr_posted = %ld\n", strerror(ret), ret, worker->rdma_wr_posted);
			goto err;
		}

		if (worker->rdma_wr_posted % RDMA_REAP_FREQ == 0) {
			if (reap_rdma_cq(worker, 0, wc) < 0)
				goto err;
		}

		worker->free_io_units_rdma--;
		if (worker->free_io_units_rdma == 0) {
			if (reap_rdma_cq(worker, 1, wc) < 0)
				goto err;
		}
	}

	worker->stopped = true;
	rdma_client_terminate(worker);

	return NULL;

err:
	pthread_exit((void *)1);
	return NULL;
}

/* create a worker thread on the specified cpu */
struct io_worker *launch_worker(int cpu) {
	struct io_worker *worker = (struct io_worker *) malloc(sizeof(struct io_worker));

	memset(worker, 0, sizeof(worker));
	worker->free_io_units_io_uring = io_uring_queue_depth;
	worker->free_io_units_rdma = rdma_queue_depth;
	worker->rdma_wr_posted = 0;
	worker->worker_id = 0;
	worker->stopped = false;
	if (!client) {
		worker->data_pool_size = 32 * 1024 * 1024;
	}

	if (user_rdma)
		pthread_create(&worker->tid, NULL, do_io_user_rdma, (void *) worker);
	else
		pthread_create(&worker->tid, NULL, do_io_io_uring, (void *) worker);
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

	printf("bs=%ldB, io_uring_queue_depth=%d, test_file=%s, io_uring_batch_submit=%d, event=%d\n\n",
		bs, io_uring_queue_depth, test_file.data(), io_uring_batch_submit, event);

	if (user_rdma && !client)
		return NULL;

	while (!worker->stopped) {
		io_completed = worker->io_completed;
		duration_ms = duration_cast<milliseconds>(system_clock::now() - worker->start_time).count();
		if (io_completed == 0 || duration_ms == 0)
			continue;

		// printf("io_completed %lu, duration_ms %d\n", io_completed, duration_ms);
		thru = 1.0 * io_completed * bs / MB / duration_ms * 1000;
		kiops = io_completed / duration_ms;
		printf("\rthroughput: %f MB/s (%dK IOPS)\n", thru, kiops);
		fflush(stdout);
		sleep(1);
	}

	return NULL;
}

int parse_args(int argc, char *argv[]) {
	argparse::ArgumentParser program;
	program.add_argument("-bs").help("block size for each I/O in KB").default_value(4).scan<'i', int>();
	program.add_argument("-file").help("test file on which we do the I/Os").default_value(std::string("/mnt/test"));
	program.add_argument("-iouring-qd").help("io_uring queue sq depth").default_value(128).scan<'i', int>();
	program.add_argument("-rdma-qd").help("rdma queue depth").default_value(128).scan<'i', int>();
	program.add_argument("-runtime").help("runtime of the benchmark").default_value(30).scan<'i', int>();
	program.add_argument("-filesize").help("size of the test file in GB").default_value(10).scan<'i', int>();
	program.add_argument("-iouring-batch-submit").help("io_uring sqe submission batch").default_value(32).scan<'i', int>();
	program.add_argument("-rdma-batch-submit").help("rdma wqe submittion batch").default_value(32).scan<'i', int>();
	program.add_argument("-client").help("rdma io_uring client").default_value(false).implicit_value(true);
	program.add_argument("-host").help("host address for rdma").default_value(std::string("10.10.1.2"));
	program.add_argument("-port").help("port number for rdma").default_value(9876).scan<'i', int>();
	program.add_argument("-user-rdma").help("perform the userspace rdma benchmark, default is NVMeoF").default_value(false).implicit_value(true);
	program.add_argument("-event").help("sleep on cq events, default is poll").default_value(false).implicit_value(true);

	if (argc == 1) {
		printf("%s\n", program.help().str().data());
		return 1;
	}
	try {
		program.parse_args(argc, argv);

		bs = program.get<int>("-bs") * KB;
		test_file = program.get<std::string>("-file");
		file_size = program.get<int>("-filesize") * GB;
		io_uring_queue_depth = program.get<int>("-iouring-qd");
		rdma_queue_depth = program.get<int>("-rdma-qd");
		runtime = program.get<int>("-runtime");
		io_uring_batch_submit = program.get<int>("-iouring-batch-submit");
		rdma_batch_submit = program.get<int>("-rdma-batch-submit");
		client = program.get<bool>("-client");
		host = program.get<std::string>("-host");
		base_port = program.get<int>("-port");
		user_rdma = program.get<bool>("-user-rdma");
		event = program.get<bool>("-event");
	} catch (std::exception &e) {
		printf("%s\n", e.what());
		printf("%s\n", program.help().str().data());
		return 1;
	}

	return 0;
}


int main(int argc, char *argv[]) {
	struct io_worker *worker;
	pthread_t monitor_thread;

	if (parse_args(argc, argv))
		return 1;
	
	worker = launch_worker(0);
	monitor_thread = pthread_create(&monitor_thread, NULL, calculate_stats, (void *) worker);

	pthread_join(worker->tid, NULL);
	pthread_join(monitor_thread, NULL);
}