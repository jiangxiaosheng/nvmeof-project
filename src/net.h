/*
 * credit: https://insujang.github.io/2020-02-09/introduction-to-programming-infiniband/
 */

#include <infiniband/verbs.h>
#include <string>
#include <iostream>
#include <json.hpp>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

using nlohmann::json;

class RDMAPeerInfo {
public:
	uint16_t local_id;
	uint32_t qp_number;
};

void to_json(json &j, const RDMAPeerInfo &info) {
	j = json{{"local_id", info.local_id}, {"qp_number", info.qp_number}};
}

void from_json(const json &j, RDMAPeerInfo &info) {
	j.at("local_id").get_to(info.local_id);
	j.at("qp_number").get_to(info.qp_number);
}

enum {
	INITIATOR,
	TARGET,
};

uint16_t get_local_id(struct ibv_context *context, int ib_port) {
	ibv_port_attr port_attr;
  	ibv_query_port(context, ib_port, &port_attr);
  	return port_attr.lid;
}

uint32_t get_queue_pair_number(struct ibv_qp* qp) {
  return qp->qp_num;
}

struct ibv_context* create_context(const std::string& device_name) {
  /* There is no way to directly open the device with its name; we should get the list of devices first. */
  struct ibv_context* context = nullptr;
  int num_devices;
  struct ibv_device** device_list = ibv_get_device_list(&num_devices);
  for (int i = 0; i < num_devices; i++){
    /* match device name. open the device and return it */
    if (device_name.compare(ibv_get_device_name(device_list[i])) == 0) {
      context = ibv_open_device(device_list[i]);
      break;
    }
  }

  /* it is important to free the device list; otherwise memory will be leaked. */
  ibv_free_device_list(device_list);
  if (context == nullptr) {
    std::cerr << "Unable to find the device " << device_name << std::endl;
  }
  return context;
}

struct ibv_qp *create_queue_pair(struct ibv_pd *pd, struct ibv_cq *cq) {
	struct ibv_qp_init_attr queue_pair_init_attr;
	memset(&queue_pair_init_attr, 0, sizeof(queue_pair_init_attr));
	queue_pair_init_attr.qp_type = IBV_QPT_RC;
	queue_pair_init_attr.sq_sig_all = 1;       // if not set 0, all work requests submitted to SQ will always generate a Work Completion.
	queue_pair_init_attr.send_cq = cq;         // completion queue can be shared or you can use distinct completion queues.
	queue_pair_init_attr.recv_cq = cq;         // completion queue can be shared or you can use distinct completion queues.
	queue_pair_init_attr.cap.max_send_wr = 1;  // increase if you want to keep more send work requests in the SQ.
	queue_pair_init_attr.cap.max_recv_wr = 1;  // increase if you want to keep more receive work requests in the RQ.
	queue_pair_init_attr.cap.max_send_sge = 1; // increase if you allow send work requests to have multiple scatter gather entry (SGE).
	queue_pair_init_attr.cap.max_recv_sge = 1; // increase if you allow receive work requests to have multiple scatter gather entry (SGE).

	return ibv_create_qp(pd, &queue_pair_init_attr);
}

bool change_queue_pair_state_to_init(struct ibv_qp* queue_pair) {
  struct ibv_qp_attr init_attr;
  memset(&init_attr, 0, sizeof(init_attr));
  init_attr.qp_state = ibv_qp_state::IBV_QPS_INIT;
  init_attr.port_num = 1; // port number is 1 on my machine
  init_attr.pkey_index = 0;
  init_attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;

  return ibv_modify_qp(queue_pair, &init_attr, IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS) == 0;
}

bool change_queue_pair_state_to_rtr(struct ibv_qp* queue_pair, int ib_port, uint32_t destination_qp_number, uint16_t destination_local_id) {
  struct ibv_qp_attr rtr_attr;
  memset(&rtr_attr, 0, sizeof(rtr_attr));
  rtr_attr.qp_state = ibv_qp_state::IBV_QPS_RTR;
  rtr_attr.path_mtu = ibv_mtu::IBV_MTU_1024;
  rtr_attr.rq_psn = 0;
  rtr_attr.max_dest_rd_atomic = 1;
  rtr_attr.min_rnr_timer = 0x12;
  rtr_attr.ah_attr.is_global = 0;
  rtr_attr.ah_attr.sl = 0;
  rtr_attr.ah_attr.src_path_bits = 0;
  rtr_attr.ah_attr.port_num = ib_port;
  
  rtr_attr.dest_qp_num = destination_qp_number;
  rtr_attr.ah_attr.dlid = destination_local_id;

  return ibv_modify_qp(queue_pair, &rtr_attr, IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER) == 0;
}

bool change_queue_pair_state_to_rts(struct ibv_qp* queue_pair) {
  struct ibv_qp_attr rts_attr;
  memset(&rts_attr, 0, sizeof(rts_attr));
  rts_attr.qp_state = ibv_qp_state::IBV_QPS_RTS;
  rts_attr.timeout = 0x12;
  rts_attr.retry_cnt = 7;
  rts_attr.rnr_retry = 7;
  rts_attr.sq_psn = 0;
  rts_attr.max_rd_atomic = 1;

  return ibv_modify_qp(queue_pair, &rts_attr, IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC);
}

// use TCP socket to establish connection
RDMAPeerInfo exchange_peer_info(int role, const std::string &address, const RDMAPeerInfo &my_info) {
	RDMAPeerInfo peer_info;

	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		std::cerr << "open socket failed" << std::endl;
		return peer_info;
	}

	if (role == TARGET) {
		// target first sends my_info to client

		struct sockaddr_in addr;
		memset(&addr, 0, sizeof(addr));

		addr.sin_family = AF_INET;
		addr.sin_port = htons(9876);
		addr.sin_addr.s_addr = INADDR_ANY;

		if (bind(sock, (struct sockaddr *) &addr, sizeof(addr))) {
			std::cerr << "bind failed" << std::endl;
			goto ret;
		}

		if (listen(sock, 5)) {
			std::cerr << "listen failed" << std::endl;
			goto ret;
		}

		int client_sock = accept(sock, nullptr, nullptr);
		if (client_sock < 0) {
			std::cerr << "accept failed" << std::endl;
			goto ret;
		}

		json j = my_info;
		std::string buffer = j.dump();
		int len = buffer.size() + 1;
		
		int size = send(client_sock, buffer.c_str(), len, 0);
		if (size != len) {
			std::cerr << "send failed" << std::endl;
			goto ret;
		}

		return peer_info;
	} else if (role == INITIATOR) {
		struct sockaddr_in addr;
		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_port = htons(9876);
		addr.sin_addr.s_addr = inet_addr(address.c_str());

		if (connect(sock, (struct sockaddr*) &addr, sizeof(addr)) < 0) {
			std::cerr << "connect failed" << std::endl;
			goto ret;
		}

		char buffer[1024];
		int len = recv(sock, buffer, sizeof(buffer), 0);
		if (len < 0) {
			std::cerr << "recv failed" << std::endl;
			goto ret;
		}
		buffer[len] = 0;
		
		json j = json::parse(buffer);
		
		peer_info = j.get<RDMAPeerInfo>();

		return peer_info;
	}

ret:
	close(sock);

	return peer_info;
}

int init_network(const std::string &device_name, int role, const std::string &address) {
	int ib_port = 1; // on my machine it's 1

	struct ibv_context *context = create_context(device_name);
	if (context == NULL)
		return -1;
	
	struct ibv_pd *pd = ibv_alloc_pd(context);
	if (pd == nullptr) {
		std::cerr << "allocate protection domain failed" << std::endl;
		return -1;
	}

	int cq_size = 0x10;

	struct ibv_cq *cq = ibv_create_cq(context, cq_size, nullptr, nullptr, 0);
	if (cq == nullptr) {
		std::cerr << "create completion queue failed" << std::endl;
		return -1;
	}

	struct ibv_qp *qp = create_queue_pair(pd, cq);

	RDMAPeerInfo my_info, peer_info;
	my_info.local_id = get_local_id(context, ib_port);
	my_info.qp_number = get_queue_pair_number(qp);

	peer_info = exchange_peer_info(role, address, my_info);

	if (!change_queue_pair_state_to_init(qp)) {
		std::cerr << "change queue pair state to init failed" << std::endl;
		return -1;
	}

	if (!change_queue_pair_state_to_rtr(qp, ib_port, peer_info.qp_number, peer_info.local_id)) {
		std::cerr << "change queue pair state to init failed" << std::endl;
		return -1;
	}

	if (!change_queue_pair_state_to_rts(qp)) {
		std::cerr << "change queue pair state to init failed" << std::endl;
		return -1;
	}

	

	return 0;
}