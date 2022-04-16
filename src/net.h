#pragma once

#include <infiniband/verbs.h>
#include <string>
#include <iostream>
// #include <json.hpp>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <chrono>
#include <sstream>
#include <thread>

#include "nvme.h"
#include "net.grpc.pb.h"
#include "grpc/grpc.h"
#include "grpcpp/server_builder.h"
#include "grpcpp/security/server_credentials.h"
#include "thread_pool.hpp"

// using nlohmann::json;

enum {
	INITIATOR,
	TARGET,
};

class Connection {
public:
	virtual int init() = 0;
	virtual int send(void *data, int len) = 0;
	virtual int recv(void *data, int len) = 0;
	virtual int close() = 0;
};

/*
 * credit: https://insujang.github.io/2020-02-09/introduction-to-programming-infiniband/
 */
// class RDMAConnection : public Connection {
// private:
// 	class RDMAPeerInfo {
// 	public:
// 		uint16_t local_id;
// 		uint32_t qp_number;
// 	};

// 	void to_json(json &j, const RDMAPeerInfo &info) {
// 		j = json{{"local_id", info.local_id}, {"qp_number", info.qp_number}};
// 	}

// 	void from_json(const json &j, RDMAPeerInfo &info) {
// 		j.at("local_id").get_to(info.local_id);
// 		j.at("qp_number").get_to(info.qp_number);
// 	}

// 	uint16_t get_local_id(struct ibv_context *context, int ib_port) {
// 		ibv_port_attr port_attr;
// 		ibv_query_port(context, ib_port, &port_attr);
// 		return port_attr.lid;
// 	}

// 	uint32_t get_queue_pair_number(struct ibv_qp* qp) {
// 		return qp->qp_num;
// 	}

// 	struct ibv_context* create_context(const std::string& device_name) {
// 		/* There is no way to directly open the device with its name; we should get the list of devices first. */
// 		struct ibv_context* context = nullptr;
// 		int num_devices;
// 		struct ibv_device** device_list = ibv_get_device_list(&num_devices);
// 		for (int i = 0; i < num_devices; i++){
// 			/* match device name. open the device and return it */
// 			if (device_name.compare(ibv_get_device_name(device_list[i])) == 0) {
// 				context = ibv_open_device(device_list[i]);
// 				break;
// 			}
// 		}

// 		/* it is important to free the device list; otherwise memory will be leaked. */
// 		ibv_free_device_list(device_list);
// 		if (context == nullptr) {
// 			std::cerr << "Unable to find the device " << device_name << std::endl;
// 		}
// 		return context;
// 	}

// 	struct ibv_qp *create_queue_pair(struct ibv_pd *pd, struct ibv_cq *cq) {
// 		struct ibv_qp_init_attr queue_pair_init_attr;
// 		memset(&queue_pair_init_attr, 0, sizeof(queue_pair_init_attr));
// 		queue_pair_init_attr.qp_type = IBV_QPT_RC;
// 		queue_pair_init_attr.sq_sig_all = 1;       // if not set 0, all work requests submitted to SQ will always generate a Work Completion.
// 		queue_pair_init_attr.send_cq = cq;         // completion queue can be shared or you can use distinct completion queues.
// 		queue_pair_init_attr.recv_cq = cq;         // completion queue can be shared or you can use distinct completion queues.
// 		queue_pair_init_attr.cap.max_send_wr = 1;  // increase if you want to keep more send work requests in the SQ.
// 		queue_pair_init_attr.cap.max_recv_wr = 1;  // increase if you want to keep more receive work requests in the RQ.
// 		queue_pair_init_attr.cap.max_send_sge = 1; // increase if you allow send work requests to have multiple scatter gather entry (SGE).
// 		queue_pair_init_attr.cap.max_recv_sge = 1; // increase if you allow receive work requests to have multiple scatter gather entry (SGE).

// 		return ibv_create_qp(pd, &queue_pair_init_attr);
// 	}

// 	bool change_queue_pair_state_to_init(struct ibv_qp* queue_pair) {
// 		struct ibv_qp_attr init_attr;
// 		memset(&init_attr, 0, sizeof(init_attr));
// 		init_attr.qp_state = ibv_qp_state::IBV_QPS_INIT;
// 		init_attr.port_num = 1; // port number is 1 on my machine
// 		init_attr.pkey_index = 0;
// 		init_attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;

// 		return ibv_modify_qp(queue_pair, &init_attr, IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS) == 0;
// 	}

// 	bool change_queue_pair_state_to_rtr(struct ibv_qp* queue_pair, int ib_port, uint32_t destination_qp_number, uint16_t destination_local_id) {
// 		struct ibv_qp_attr rtr_attr;
// 		memset(&rtr_attr, 0, sizeof(rtr_attr));
// 		rtr_attr.qp_state = ibv_qp_state::IBV_QPS_RTR;
// 		rtr_attr.path_mtu = ibv_mtu::IBV_MTU_1024;
// 		rtr_attr.rq_psn = 0;
// 		rtr_attr.max_dest_rd_atomic = 1;
// 		rtr_attr.min_rnr_timer = 0x12;
// 		rtr_attr.ah_attr.is_global = 0;
// 		rtr_attr.ah_attr.sl = 0;
// 		rtr_attr.ah_attr.src_path_bits = 0;
// 		rtr_attr.ah_attr.port_num = ib_port;
		
// 		rtr_attr.dest_qp_num = destination_qp_number;
// 		rtr_attr.ah_attr.dlid = destination_local_id;

// 		return ibv_modify_qp(queue_pair, &rtr_attr, IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER) == 0;
// 	}

// 	bool change_queue_pair_state_to_rts(struct ibv_qp* queue_pair) {
// 		struct ibv_qp_attr rts_attr;
// 		memset(&rts_attr, 0, sizeof(rts_attr));
// 		rts_attr.qp_state = ibv_qp_state::IBV_QPS_RTS;
// 		rts_attr.timeout = 0x12;
// 		rts_attr.retry_cnt = 7;
// 		rts_attr.rnr_retry = 7;
// 		rts_attr.sq_psn = 0;
// 		rts_attr.max_rd_atomic = 1;

// 		return ibv_modify_qp(queue_pair, &rts_attr, IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC);
// 	}

// 	// use TCP socket to establish connection
// 	RDMAPeerInfo exchange_peer_info(int role, const std::string &address, const RDMAPeerInfo &my_info) {
// 		RDMAPeerInfo peer_info;

// 		int sock = socket(AF_INET, SOCK_STREAM, 0);
// 		if (sock < 0) {
// 			std::cerr << "open socket failed" << std::endl;
// 			return peer_info;
// 		}

// 		if (role == TARGET) {
// 			// target first sends my_info to client

// 			struct sockaddr_in addr;
// 			memset(&addr, 0, sizeof(addr));

// 			addr.sin_family = AF_INET;
// 			addr.sin_port = htons(9876);
// 			addr.sin_addr.s_addr = INADDR_ANY;

// 			if (bind(sock, (struct sockaddr *) &addr, sizeof(addr))) {
// 				std::cerr << "bind failed" << std::endl;
// 				goto ret;
// 			}

// 			if (listen(sock, 5)) {
// 				std::cerr << "listen failed" << std::endl;
// 				goto ret;
// 			}

// 			int client_sock = ::accept(sock, nullptr, nullptr);
// 			if (client_sock < 0) {
// 				std::cerr << "accept failed" << std::endl;
// 				goto ret;
// 			}

// 			json j;
// 			to_json(j, my_info);
// 			std::string buffer = j.dump();
// 			int len = buffer.size() + 1;
// 			std::cout << "target's rdma info: " << buffer << std::endl;
			
// 			int size = ::send(client_sock, buffer.c_str(), len, 0);
// 			if (size != len) {
// 				std::cerr << "send failed" << std::endl;
// 				goto ret;
// 			}

// 			return peer_info;
// 		} else if (role == INITIATOR) {
// 			struct sockaddr_in addr;
// 			memset(&addr, 0, sizeof(addr));
// 			addr.sin_family = AF_INET;
// 			addr.sin_port = htons(9876);
// 			addr.sin_addr.s_addr = inet_addr(address.c_str());

// 			if (connect(sock, (struct sockaddr*) &addr, sizeof(addr)) < 0) {
// 				std::cerr << "connect failed" << std::endl;
// 				goto ret;
// 			}

// 			char buffer[1024];
// 			int len = ::recv(sock, buffer, sizeof(buffer), 0);
// 			if (len < 0) {
// 				std::cerr << "recv failed" << std::endl;
// 				goto ret;
// 			}
// 			buffer[len] = 0;

// 			std::cout << buffer << std::endl;
			
// 			json j = json::parse(buffer);
			
// 			from_json(j, peer_info);

// 			return peer_info;
// 		}

// 	ret:
// 		::close(sock);

// 		return peer_info;
// 	}


// 	std::string &address;
// 	std::string &device_name;
// 	int ib_port;
// 	int role;

// public:
// 	RDMAConnection(int role, std::string &device_name, std::string &address, int ib_port):
// 		role(role), device_name(device_name), address(address), ib_port(ib_port) {}
	
// 	int init() {
// 		struct ibv_context *context = create_context(device_name);
// 		if (context == NULL)
// 			return -1;
		
// 		struct ibv_pd *pd = ibv_alloc_pd(context);
// 		if (pd == nullptr) {
// 			std::cerr << "allocate protection domain failed" << std::endl;
// 			return -1;
// 		}

// 		int cq_size = 0x10;

// 		struct ibv_cq *cq = ibv_create_cq(context, cq_size, nullptr, nullptr, 0);
// 		if (cq == nullptr) {
// 			std::cerr << "create completion queue failed" << std::endl;
// 			return -1;
// 		}

// 		struct ibv_qp *qp = create_queue_pair(pd, cq);

// 		RDMAPeerInfo my_info, peer_info;
// 		my_info.local_id = get_local_id(context, ib_port);
// 		my_info.qp_number = get_queue_pair_number(qp);

// 		peer_info = exchange_peer_info(role, address, my_info);

// 		if (!change_queue_pair_state_to_init(qp)) {
// 			std::cerr << "change queue pair state to init failed" << std::endl;
// 			return -1;
// 		}

// 		if (!change_queue_pair_state_to_rtr(qp, ib_port, peer_info.qp_number, peer_info.local_id)) {
// 			std::cerr << "change queue pair state to init failed" << std::endl;
// 			return -1;
// 		}

// 		if (!change_queue_pair_state_to_rts(qp)) {
// 			std::cerr << "change queue pair state to init failed" << std::endl;
// 			return -1;
// 		}

		

// 		return 0;
// 	}

// 	int send(void *data, int len) {
// 		return 0;
// 	}

// 	int recv(void *data, int len) {
// 		return 0;
// 	}

// 	int close() {
// 		return 0;
// 	}
// };

class TCPConnection : public Connection {
public:
	TCPConnection(int role, std::string &address, int port):
		role(role), address(address), port(port) {}

	TCPConnection(int role, int port): role(role), port(port) {}

	int init() {
		if (role == TARGET) {
			int sock = socket(AF_INET, SOCK_STREAM, 0);
			if (sock < 0) {
				std::cerr << "open socket failed" << std::endl;
			}

			struct sockaddr_in addr;
			memset(&addr, 0, sizeof(addr));

			addr.sin_family = AF_INET;
			addr.sin_port = htons(port);
			addr.sin_addr.s_addr = INADDR_ANY;

			if (bind(sock, (struct sockaddr *) &addr, sizeof(addr))) {
				std::cerr << "bind failed" << std::endl;
				goto ret;
			}

			if (listen(sock, 5)) {
				std::cerr << "listen failed" << std::endl;
				goto ret;
			}

			peer_sock = ::accept(sock, nullptr, nullptr);
			if (peer_sock < 0) {
				std::cerr << "accept failed" << std::endl;
				goto ret;
			}

			::close(sock);

		} else if (role == INITIATOR) {
			peer_sock = socket(AF_INET, SOCK_STREAM, 0);

			struct sockaddr_in addr;
			memset(&addr, 0, sizeof(addr));
			addr.sin_family = AF_INET;
			addr.sin_port = htons(port);
			addr.sin_addr.s_addr = inet_addr(address.c_str());
			std::cout << address << std::endl;


			if (connect(peer_sock, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
				perror("connect failed");
				goto ret;
			}
		}

		return 0;

	ret:
		return close();
	}

	int send(void *data, int len) {
		int left = len ;
        char *buffer = (char *) data;
        while (left > 0) {
			int writeBytes = ::send(peer_sock, buffer, left, 0);
			if (writeBytes < 0) {
				if (errno == EINTR)
					continue;
				return -1;
			} else if (writeBytes == 0)
				continue;
			left -= writeBytes;
			buffer += writeBytes;
        }
        return len;
	}

	int recv(void *data, int len) {
		int left = len;
        char *buffer = (char*) data;
        while (left > 0) {
			int readBytes = ::recv(peer_sock, buffer, left, 0);
			if (readBytes < 0) {
				if(errno == EINTR) {
					continue;
				}
				return -1;
			} if (readBytes == 0) {
				printf("peer close\n");
				return len - left;
			}
			left -= readBytes;
			buffer += readBytes ;
        }
        return len;
	}

	int close() {
		return ::close(peer_sock);
	}

private:
	int peer_sock;
	std::string address;
	int role;
	int port;
};

using namespace net;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using grpc::StatusCode;
using grpc::ServerCompletionQueue;
using grpc::ServerAsyncResponseWriter;

class GRPCSyncServer final : public Block::Service {
public:
	GRPCSyncServer(std::string addr, int port, std::string blk_device) : address(addr), port(port) {
		blk_fd = open(blk_device.c_str(), O_RDWR | O_DIRECT);
		if (blk_fd < 0) {
			perror("open block device failed");
			return;
		}
		config.name = const_cast<char*>(blk_device.data());
		init_device_config(config);
	}

	Status ReadBlock(ServerContext *ctx, const BlockRequest *request, BlockReply *reply)  {
		auto total_start = std::chrono::system_clock::now();
		static struct nvme_io_args args {
			.result = NULL,
			.args_size = sizeof(args),
			.fd = blk_fd,
			.timeout = NVME_DEFAULT_IOCTL_TIMEOUT,
			.nsid = config.namespace_id,
		};

		args.slba = request->start_block();
		int actual_len;

		if (request->size() % config.logical_block_size != 0) {
			args.nlb = request->size() / config.logical_block_size;
		} else {
			args.nlb = request->size() / config.logical_block_size - 1;
		}
		actual_len = (args.nlb + 1) * config.logical_block_size;
		
		args.data_len = actual_len;

		auto start = std::chrono::system_clock::now();
		args.data = new char[actual_len];

		int buffer_size = actual_len;
		if (request->data().size() > actual_len)
			buffer_size = actual_len;
		memcpy(args.data, request->data().data(), request->data().size());
		time_on_allocation += std::chrono::system_clock::now() - start;
		
		// dump args
		// printf("slba = %lld\n", args.slba);
		// printf("nlb = %d\n", args.nlb);
		// printf("data_len = %d\n", args.data_len);

		
		start = std::chrono::system_clock::now();
		int err = nvme_read(&args);
		time_on_rw += std::chrono::system_clock::now() - start;
		delete[] (char *) args.data;

		time_total += std::chrono::system_clock::now() - total_start;
		if (err < 0) {
			fprintf(stderr, "submit-io: %s\n", nvme_strerror(errno));
			return Status(StatusCode::UNKNOWN, "nvme write error");
		} else if (err) {
			nvme_show_status(err);
			return Status(StatusCode::UNKNOWN, "nvme write error");
		}

		reply->set_end_block(request->start_block() + args.nlb);
		return Status::OK;
	}

	Status WriteBlock(ServerContext *ctx, const BlockRequest *request, BlockReply *reply)  {
		auto total_start = std::chrono::system_clock::now();
		static struct nvme_io_args args {
			.result = NULL,
			.args_size = sizeof(args),
			.fd = blk_fd,
			.timeout = NVME_DEFAULT_IOCTL_TIMEOUT,
			.nsid = config.namespace_id,
		};

		args.slba = request->start_block();
		int actual_len;

		if (request->size() % config.logical_block_size != 0) {
			args.nlb = request->size() / config.logical_block_size;
		} else {
			args.nlb = request->size() / config.logical_block_size - 1;
		}
		actual_len = (args.nlb + 1) * config.logical_block_size;
		
		args.data_len = actual_len;

		auto start = std::chrono::system_clock::now();
		args.data = new char[actual_len];

		int buffer_size = actual_len;
		if (request->data().size() > actual_len)
			buffer_size = actual_len;
		memcpy(args.data, request->data().data(), request->data().size());
		time_on_allocation += std::chrono::system_clock::now() - start;
		
		// dump args
		// printf("slba = %lld\n", args.slba);
		// printf("nlb = %d\n", args.nlb);
		// printf("data_len = %d\n", args.data_len);

		
		start = std::chrono::system_clock::now();
		int err = nvme_write(&args);
		time_on_rw += std::chrono::system_clock::now() - start;
		delete[] (char *) args.data;

		time_total += std::chrono::system_clock::now() - total_start;
		if (err < 0) {
			fprintf(stderr, "submit-io: %s\n", nvme_strerror(errno));
			return Status(StatusCode::UNKNOWN, "nvme write error");
		} else if (err) {
			nvme_show_status(err);
			return Status(StatusCode::UNKNOWN, "nvme write error");
		}

		reply->set_end_block(request->start_block() + args.nlb);
		return Status::OK;
	}

	Status GetStat(ServerContext *context, const StatRequest *request, StatReply *response) {
		std::stringstream ss;
		ss << "time on allocation is " << std::chrono::duration_cast<std::chrono::milliseconds>(time_on_allocation.time_since_epoch()).count()
			<< " ms\n" << "time on writes is " 
			<< std::chrono::duration_cast<std::chrono::milliseconds>(time_on_rw.time_since_epoch()).count()
			<< " ms\n" << "total time on server is "
			<< std::chrono::duration_cast<std::chrono::milliseconds>(time_total.time_since_epoch()).count()
			<< " ms\n";
		response->set_stat(ss.str());
		return Status::OK;
	}

	Status ResetStat(ServerContext *context, const StatRequest *request, StatReply *response) {
		time_on_allocation = time_on_rw = time_total = {};
		return Status::OK;
	}

	int run() {
		if (blk_fd < 0) {
			std::cerr << "nvme config init failed" << std::endl;
			return -1;
		}

		std::string server_addr = address + ":" + std::to_string(port);

		ServerBuilder builder;
		builder.AddListeningPort(server_addr, grpc::InsecureServerCredentials());
		builder.RegisterService(this);
		std::unique_ptr<Server> server(builder.BuildAndStart());
		std::cout << "Server listening on " << server_addr << std::endl;
  		server->Wait();

		return 0;
	}

	
private:
	std::string address;
	int port;
	int blk_fd;
	struct device_config config;
	decltype(std::chrono::system_clock::now()) time_on_allocation, time_on_rw, time_total;
};

class GRPCAsyncServer {
public:
	~GRPCAsyncServer() {
		server->Shutdown();
		write_block_handler->shutdown();
		read_block_handler->shutdown();
		get_stat_handler->shutdown();
		reset_stat_handler->shutdown();
	}

	GRPCAsyncServer(std::string addr, int port, std::string blk_device) : address(std::move(addr)), port(port) {
		// init nvme config
		blk_fd = open(blk_device.c_str(), O_RDWR | O_DIRECT);
		if (blk_fd < 0) {
			perror("open block device failed");
			return;
		}
		config.name = const_cast<char*>(blk_device.data());
		init_device_config(config);
	}

	void run() {
		std::string server_addr = address + ":" + std::to_string(port);
		ServerBuilder builder;
		builder.AddListeningPort(server_addr, grpc::InsecureServerCredentials());
		builder.RegisterService(&service);

		auto write_block_cq = builder.AddCompletionQueue();
		// auto read_block_cq = builder.AddCompletionQueue();
		// auto get_stat_cq = builder.AddCompletionQueue();
		// auto reset_stat_cq = builder.AddCompletionQueue();
		server = builder.BuildAndStart();
		
		write_block_handler = std::make_unique<RPCHandler<WriteBlockCallData>>(this, &service, write_block_cq.release());
		// read_block_handler = std::make_unique<RPCHandler<ReadBlockCallData>>(this, &service, builder.AddCompletionQueue().release());
		// get_stat_handler = std::make_unique<RPCHandler<GetStatCallData>>(this, &service, builder.AddCompletionQueue().release());
		// reset_stat_handler = std::make_unique<RPCHandler<ResetStatCallData>>(this, &service, builder.AddCompletionQueue().release());

		write_block_handler->exec();
		// read_block_handler->exec();
		// get_stat_handler->exec();
		// reset_stat_handler->exec();

		// std::cout << "ok" << std::endl;
		
		// std::cout << "Server listening on " << server_addr << std::endl;


		// yield cpu
		std::promise<void>().get_future().wait();
	}

private:
	class CallData {
	public:
		enum class CallStatus { CREATE, PROCESS, FINISH };

		CallData(GRPCAsyncServer *server, Block::AsyncService *service, ServerCompletionQueue *cq)
			: server(server), service(service), cq(cq), status(CallStatus::CREATE) {}

		virtual void proceed() = 0;

	protected:
		Block::AsyncService *service;
		ServerCompletionQueue *cq;
		ServerContext ctx;
		CallStatus status;
		GRPCAsyncServer *server;
	};

	class WriteBlockCallData : public CallData {
	public:
		WriteBlockCallData(GRPCAsyncServer *server, Block::AsyncService *service, ServerCompletionQueue *cq) : 
			CallData(server, service, cq), responder(&ctx) {
			proceed();
		}

		void proceed() override {
			if (status == CallStatus::CREATE) {
				status = CallStatus::PROCESS;
				service->RequestWriteBlock(&ctx, &request, &responder, cq, cq, this);
			} else if (status == CallStatus::PROCESS) {
				new WriteBlockCallData(server, service, cq);

				status = CallStatus::FINISH;
				reply.set_end_block(114514);
				responder.Finish(reply, Status::OK, this);
			} else {
				std::cout << "delete " << this << std::endl;
				GPR_ASSERT(status == CallStatus::FINISH);
				delete this;
			}
		}

	private:
		BlockRequest request;
		BlockReply reply;
		ServerAsyncResponseWriter<BlockReply> responder;
	};

	class ReadBlockCallData : public CallData {
	public:
		ReadBlockCallData(GRPCAsyncServer *server, Block::AsyncService *service, ServerCompletionQueue *cq) : 
			CallData(server, service, cq), responder(&ctx) {
			proceed();
		}

		void proceed() override {
			if (status == CallStatus::CREATE) {
				status = CallStatus::PROCESS;
				service->RequestWriteBlock(&ctx, &request, &responder, cq, cq, this);
			} else if (status == CallStatus::PROCESS) {
				std::cout << "async server start processing..." << std::endl;
				new WriteBlockCallData(server, service, cq);

				auto total_start = std::chrono::system_clock::now();
				static struct nvme_io_args args {
					.result = NULL,
					.args_size = sizeof(args),
					.fd = server->blk_fd,
					.timeout = NVME_DEFAULT_IOCTL_TIMEOUT,
					.nsid = server->config.namespace_id,
				};

				args.slba = request.start_block();
				int actual_len;

				if (request.size() % server->config.logical_block_size != 0) {
					args.nlb = request.size() / server->config.logical_block_size;
				} else {
					args.nlb = request.size() / server->config.logical_block_size - 1;
				}
				actual_len = (args.nlb + 1) * server->config.logical_block_size;
				
				args.data_len = actual_len;

				auto start = std::chrono::system_clock::now();
				args.data = new char[actual_len];

				int buffer_size = actual_len;
				if (request.data().size() > actual_len)
					buffer_size = actual_len;
				memcpy(args.data, request.data().data(), request.data().size());
				server->time_on_allocation += std::chrono::system_clock::now() - start;

				start = std::chrono::system_clock::now();
				int err = nvme_read(&args);
				server->time_on_rw += std::chrono::system_clock::now() - start;
				delete[] (char *) args.data;

				server->time_total += std::chrono::system_clock::now() - total_start;

				status = CallStatus::FINISH;
				if (err < 0) {
					fprintf(stderr, "submit-io: %s\n", nvme_strerror(errno));
					responder.Finish(reply, Status(StatusCode::UNKNOWN, "nvme write error"), this);
					return;
				} else if (err) {
					nvme_show_status(err);
					responder.Finish(reply, Status(StatusCode::UNKNOWN, "nvme write error"), this);
					return;
				}

				reply.set_end_block(request.start_block() + args.nlb);
				responder.Finish(reply, Status::OK, this);
			} else {
				GPR_ASSERT(status == CallStatus::FINISH);
				delete this;
			}
		}

	private:
		BlockRequest request;
		BlockReply reply;
		ServerAsyncResponseWriter<BlockReply> responder;
	};

	class GetStatCallData : public CallData {
	public:
		GetStatCallData(GRPCAsyncServer *server, Block::AsyncService *service, ServerCompletionQueue *cq) : 
			CallData(server, service, cq), responder(&ctx) {
			proceed();
		}

	void proceed() override {

	}

	private:
		StatRequest request;
		StatReply reply;
		ServerAsyncResponseWriter<StatReply> responder;
	};

	class ResetStatCallData : public CallData {
	public:
		ResetStatCallData(GRPCAsyncServer *server, Block::AsyncService *service, ServerCompletionQueue *cq) : 
			CallData(server, service, cq), responder(&ctx) {
			proceed();
		}

		void proceed() override {

		}

	private:
		StatRequest request;
		StatReply reply;
		ServerAsyncResponseWriter<StatReply> responder;
	};

	template<typename CallData>
	class RPCHandler {
	public:
		RPCHandler(GRPCAsyncServer *server, Block::AsyncService *service, ServerCompletionQueue *cq_) : 
			service(service), cq(cq_), server(server) {
			tp = std::make_unique<thread_pool>();
		}

		void exec() {
			daemon = std::make_unique<std::thread>([&]() {
				new CallData(server, service, cq);
				void *tag;
				bool ok;
				auto worker_fn = std::mem_fn(&CallData::proceed);
				while (true) {
					GPR_ASSERT(cq->Next(&tag, &ok));
					GPR_ASSERT(ok);
					// worker_fn((CallData *) tag);
					static_cast<CallData*>(tag)->proceed();
					// tp->push_task(worker_fn, (CallData *) tag);
				}
			});
		}

		
		void shutdown() {
			cq->Shutdown();
			if (daemon != nullptr)
				daemon->join();
		}

	private:
		Block::AsyncService *service;
		std::unique_ptr<thread_pool> tp;
		ServerCompletionQueue *cq;
		std::unique_ptr<std::thread> daemon;
		GRPCAsyncServer *server;
	};

	Block::AsyncService service;
	std::unique_ptr<Server> server;

	std::unique_ptr<RPCHandler<WriteBlockCallData>> write_block_handler;
	std::unique_ptr<RPCHandler<ReadBlockCallData>> read_block_handler;
	std::unique_ptr<RPCHandler<GetStatCallData>> get_stat_handler;
	std::unique_ptr<RPCHandler<ResetStatCallData>> reset_stat_handler;

	// network
	std::string address;
	int port;

	// nvme
	int blk_fd;
	struct device_config config;
	decltype(std::chrono::system_clock::now()) time_on_allocation, time_on_rw, time_total;
};