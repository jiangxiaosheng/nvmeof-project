#include "net.grpc.pb.h"
#include "net.pb.h"
#include "grpc/grpc.h"
#include "grpcpp/server_builder.h"
#include "grpcpp/security/server_credentials.h"
#include "third_party/thread_pool.hpp"
#include <liburing.h>

#include <memory>
#include <string>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unordered_map>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <cstring>
#include <sstream>
#include <mutex>

using namespace std;
using namespace net;
using namespace chrono;
namespace fs = std::filesystem;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using grpc::StatusCode;
using grpc::ServerCompletionQueue;
using grpc::ServerAsyncResponseWriter;
using grpc::ServerReaderWriter;

class GRPCFileServerSync final : public File::Service {
public:
	GRPCFileServerSync(string addr, int port) : address(addr), port(port) {}

	int run() {
		std::string server_addr = address + ":" + std::to_string(port);

		ServerBuilder builder;
		builder.AddListeningPort(server_addr, grpc::InsecureServerCredentials());
    builder.SetMaxReceiveMessageSize(INT_MAX);
		builder.RegisterService(this);
		std::unique_ptr<Server> server(builder.BuildAndStart());
		std::cout << "Server listening on " << server_addr << std::endl;
  		server->Wait();

		return 0;
	}

  // setup io_uring at the same time
	Status SetParameters(ServerContext *ctx, const Parameters *params, ACK *ack) {
		buffer_size = params->buffer_size();
		queue_depth = params->queue_depth();
		poll_threshold = params->poll_threshold();
		blk_size = params->blk_size();
    N = params->n();
    fn = params->nfiles();

		cout << "set params:\n" 
			<< "  buffer_size = " << buffer_size << "B\n"
			<< "  block_size = " << blk_size << "B\n"
			<< "  queue_depth = " << queue_depth << "\n"
			<< "  poll_threshold = " << poll_threshold << "\n"
			<< endl;

		return Status::OK;
	}

	Status AppendOneLargeFile(ServerContext *ctx, ServerReaderWriter<FileReply, FileRequest> *stream) {
    FileRequest request;
		FileReply reply;
		stream->Read(&request);

		int err;

		int fd = open(request.filename().data(), O_APPEND | O_DIRECT | O_WRONLY | O_CREAT | O_TRUNC, 0644);

		decltype(chrono::system_clock::now()) rw_start, allocation_start;
		do {
			allocation_start = chrono::system_clock::now();
			void *buffer;
			err = posix_memalign(&buffer, blk_size, buffer_size);
			if (err) {
				fprintf(stderr, "Error posix_memalign: %s\n", strerror(-err));
				return Status(StatusCode::UNKNOWN, "posix_memalign failed");
			}
      
			memcpy(buffer, request.data().data(), buffer_size);
      
			time_on_allocation += chrono::system_clock::now() - allocation_start;
			
			rw_start = chrono::system_clock::now();
			err = write(fd, buffer, buffer_size);
			time_on_rw += chrono::system_clock::now() - rw_start;
			if (err < 0) {
				perror("write");
				return Status(StatusCode::UNKNOWN, "write data failed");
			}

			stream->Write(reply);
		} while (stream->Read(&request));
		
		close(fd);
		return Status::OK;
	}

	Status AppendManySmallFiles(ServerContext *ctx, ServerReaderWriter<FileReply, FileRequest> *stream) {
		FileRequest request;
    FileReply reply;
		
		decltype(chrono::system_clock::now()) rw_start, allocation_start, open_start, total_start;

		total_start = chrono::system_clock::now();
		while (stream->Read(&request)) {	
			std::string filename = request.filename();
			
			open_start = chrono::system_clock::now();
			int fd = open(filename.data(), O_APPEND | O_DIRECT | O_WRONLY | O_CREAT, 0644);
			time_on_open += chrono::system_clock::now() - open_start;
			if (fd < 0) {
				perror(("open file " + filename + " failed").data());
				return Status(StatusCode::UNKNOWN, "open file failed");
			}

			allocation_start = chrono::system_clock::now();
      void *buffer;
			if (posix_memalign(&buffer, blk_size, buffer_size)) {
        perror("posix_memalign");
        return Status(StatusCode::UNKNOWN, "posix_memalign failed");
      }
			memcpy(buffer, request.data().data(), buffer_size);
			time_on_allocation += chrono::system_clock::now() - allocation_start;

			rw_start = chrono::system_clock::now();
			if (write(fd, buffer, buffer_size) != buffer_size) {
				std::cerr << "write data failed" << std::endl;
				return Status(StatusCode::UNKNOWN, "write data failed");
			}
			time_on_rw += chrono::system_clock::now() - rw_start;

      if (close(fd)) {
        perror("close");
        return Status(StatusCode::UNKNOWN, "close fd failed");
      }

      stream->Write(reply);
		}
		time_total += chrono::system_clock::now() - total_start;

		return Status::OK;
	}

  Status AppendOneLargeFileAsync(ServerContext *ctx, ServerReaderWriter<FileReply, FileRequest> *stream) {
    decltype(std::chrono::system_clock::now()) time_on_copy_uring, time_total_uring;
    auto total_start = system_clock::now();

    struct io_uring ring;
    struct io_uring_params uring_params;
    memset(&uring_params, 0, sizeof(uring_params));

    int ret = io_uring_queue_init_params(queue_depth, &ring, &uring_params);
    if (ret) {
        fprintf(stderr, "Unable to setup io_uring: %s\n", strerror(-ret));
        return Status(StatusCode::UNKNOWN, "io_uring_queue_init_params failed");
    }

    thread cq_thread([=](struct io_uring *ring) {
      int ret;
      void *data;
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
        data = io_uring_cqe_get_data(cqe);
        free(data);
        io_uring_cqe_seen(ring, cqe);
      }
	  }, &ring);

    FileRequest request;
    FileReply reply;
    stream->Read(&request);

    int fd = open(request.filename().data(), O_APPEND | O_DIRECT | O_WRONLY | O_CREAT, 0644);
    if (fd < 0) {
      perror("open failed");
      return Status(StatusCode::UNKNOWN, "close fd failed");
    }

    do {
      auto sqe = io_uring_get_sqe(&ring);
      if (!sqe) {
			  perror("io_uring_get_sqe");
        return Status(StatusCode::UNKNOWN, "io_uring_get_sqe failed");
		  }

      auto copy_start = chrono::system_clock::now();
      void *buffer;
			if (posix_memalign(&buffer, blk_size, buffer_size)) {
        perror("posix_memalign");
        return Status(StatusCode::UNKNOWN, "posix_memalign failed");
      }
			memcpy(buffer, request.data().data(), buffer_size);
			time_on_copy_uring += chrono::system_clock::now() - copy_start;

      io_uring_prep_write(sqe, fd, buffer, buffer_size, 0);
      io_uring_sqe_set_data(sqe, buffer);

      io_uring_submit(&ring);
    } while (stream->Read(&request));

    cq_thread.join();

    io_uring_queue_exit(&ring);
    close(fd);

    time_total_uring += std::chrono::system_clock::now() - total_start;

    std::stringstream ss;
		ss << "time on copy is " << std::chrono::duration_cast<std::chrono::milliseconds>(time_on_copy_uring.time_since_epoch()).count()
			<< " ms\n" << "total time on server is "
			<< std::chrono::duration_cast<std::chrono::milliseconds>(time_total_uring.time_since_epoch()).count()
			<< " ms\n";
    reply.set_stats(ss.str());
    stream->Write(reply);
    return Status::OK;
  }

  Status AppendOneLargeFileAsyncExpr(ServerContext *ctx, ServerReaderWriter<FileReply, FileRequest> *stream) {
    decltype(std::chrono::system_clock::now()) time_on_copy_uring, time_total_uring;
    auto total_start = system_clock::now();

    struct io_uring ring;
    struct io_uring_params uring_params;
    memset(&uring_params, 0, sizeof(uring_params));

    int ret = io_uring_queue_init_params(queue_depth, &ring, &uring_params);
    if (ret) {
        fprintf(stderr, "Unable to setup io_uring: %s\n", strerror(-ret));
        return Status(StatusCode::UNKNOWN, "io_uring_queue_init_params failed");
    }

    thread cq_thread([=](struct io_uring *ring) {
      int ret;
      void *data;
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
        data = io_uring_cqe_get_data(cqe);
        free(data);
        io_uring_cqe_seen(ring, cqe);
      }
	  }, &ring);

    FileRequest request;
    FileReply reply;

    int fds[fn];
    int nonce = dummy++;
    for (int i = 0; i < fn; i++) {
      string name = "/mnt/large-file-" + to_string(nonce) + "-" + to_string(i);
      fds[i] = open(name.data(), O_APPEND | O_DIRECT | O_WRONLY | O_CREAT, 0644);
      if (fds[i] < 0) {
        perror("open failed");
        return Status(StatusCode::UNKNOWN, "close fd failed");
      }
    }

    int n = 0;
    while (stream->Read(&request)) {
      auto sqe = io_uring_get_sqe(&ring);
      if (!sqe) {
			  perror("io_uring_get_sqe");
        return Status(StatusCode::UNKNOWN, "io_uring_get_sqe failed");
		  }

      auto copy_start = chrono::system_clock::now();
      void *buffer;
			if (posix_memalign(&buffer, blk_size, buffer_size)) {
        perror("posix_memalign");
        return Status(StatusCode::UNKNOWN, "posix_memalign failed");
      }
			memcpy(buffer, request.data().data(), buffer_size);
			time_on_copy_uring += chrono::system_clock::now() - copy_start;

      io_uring_prep_write(sqe, fds[n++ % fn], buffer, buffer_size, 0);
      io_uring_sqe_set_data(sqe, buffer);
      io_uring_submit(&ring);
    }

    cq_thread.join();

    io_uring_queue_exit(&ring);
    
    for (int i = 0; i < fn; i++) {
      close(fds[i]);
    }

    time_total_uring += std::chrono::system_clock::now() - total_start;

    std::stringstream ss;
		ss << "time on copy is " << std::chrono::duration_cast<std::chrono::milliseconds>(time_on_copy_uring.time_since_epoch()).count()
			<< " ms\n" << "total time on server is "
			<< std::chrono::duration_cast<std::chrono::milliseconds>(time_total_uring.time_since_epoch()).count()
			<< " ms\n";
    reply.set_stats(ss.str());
    stream->Write(reply);
    return Status::OK;
  }
  

  Status AppendManySmallFilesAsync(ServerContext *ctx, ServerReaderWriter<FileReply, FileRequest> *stream) {
    decltype(std::chrono::system_clock::now()) time_on_copy_uring, time_total_uring, time_on_open_uring;
    auto total_start = system_clock::now();

    struct io_uring ring;
    struct io_uring_params uring_params;
    memset(&uring_params, 0, sizeof(uring_params));

    int ret = io_uring_queue_init_params(queue_depth, &ring, &uring_params);
    if (ret) {
        fprintf(stderr, "Unable to setup io_uring: %s\n", strerror(-ret));
        return Status(StatusCode::UNKNOWN, "io_uring_queue_init_params failed");
    }
    
    thread cq_thread([=](struct io_uring *ring) {
      int ret;
      void *data;
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
        data = io_uring_cqe_get_data(cqe);
        if (data != nullptr) {
          free(data);
        }
        io_uring_cqe_seen(ring, cqe);
      }
	  }, &ring);

    FileRequest request;
    FileReply reply;
    stream->Read(&request);

    do {
      auto open_start = system_clock::now();
      int fd = open(request.filename().data(), O_APPEND | O_DIRECT | O_WRONLY | O_CREAT, 0644);
      if (fd < 0) {
        perror("open failed");
        return Status(StatusCode::UNKNOWN, "close fd failed");
      }
      time_on_open_uring += system_clock::now() - open_start;

      auto sqe = io_uring_get_sqe(&ring);
      while (!sqe) {
			  sqe = io_uring_get_sqe(&ring);
		  }

      auto copy_start = chrono::system_clock::now();
      void *buffer;
			if (posix_memalign(&buffer, blk_size, buffer_size)) {
        perror("posix_memalign");
        return Status(StatusCode::UNKNOWN, "posix_memalign failed");
      }
			memcpy(buffer, request.data().data(), buffer_size);
			time_on_copy_uring += chrono::system_clock::now() - copy_start;

      io_uring_prep_write(sqe, fd, buffer, buffer_size, 0);
      io_uring_sqe_set_data(sqe, buffer);
      sqe->flags |= IOSQE_IO_LINK;

      sqe = io_uring_get_sqe(&ring);
      while (!sqe) {
			  sqe = io_uring_get_sqe(&ring);
		  }

      io_uring_prep_close(sqe, fd);

      io_uring_submit(&ring);
    } while (stream->Read(&request));

    cq_thread.join();

    io_uring_queue_exit(&ring);

    time_total_uring += std::chrono::system_clock::now() - total_start;

    std::stringstream ss;
		ss << "time on copy is " << std::chrono::duration_cast<std::chrono::milliseconds>(time_on_copy_uring.time_since_epoch()).count()
			<< " ms\n" << "time on open files is "
			<< std::chrono::duration_cast<std::chrono::milliseconds>(time_on_open_uring.time_since_epoch()).count()
			<< " ms\n" << "total time on server is "
			<< std::chrono::duration_cast<std::chrono::milliseconds>(time_total_uring.time_since_epoch()).count()
			<< " ms\n";
    reply.set_stats(ss.str());
    stream->Write(reply);

    return Status::OK;
  }

  Status GetStat(ServerContext *ctx, const StatRequest *request, StatReply *reply) {
		std::stringstream ss;
		ss << "time on allocation is " << std::chrono::duration_cast<std::chrono::milliseconds>(time_on_allocation.time_since_epoch()).count()
			<< " ms\n" << "time on writes is " 
			<< std::chrono::duration_cast<std::chrono::milliseconds>(time_on_rw.time_since_epoch()).count()
			<< " ms\n" << "time on open files is "
			<< std::chrono::duration_cast<std::chrono::milliseconds>(time_on_open.time_since_epoch()).count()
			<< " ms\n" << "total time on server is "
			<< std::chrono::duration_cast<std::chrono::milliseconds>(time_total.time_since_epoch()).count()
			<< " ms\n";
		cout << ss.str();
		reply->set_stat(ss.str());
		return Status::OK;
	}

	Status ResetStat(ServerContext *ctx, const StatRequest *request, StatReply *reply) {
		time_on_allocation = time_on_rw = time_total = time_on_open = {};
		return Status::OK;
	}

private:
	string address;
	int port;

	int queue_depth;
	int poll_threshold;
  decltype(std::chrono::system_clock::now()) time_on_allocation, time_on_rw, time_total, time_on_open;
	size_t buffer_size;
	int blk_size;
  int N;

  // for experiment
  int fn;
  atomic_int dummy = 0;
};

int main(int argc, char **argv) {
	GRPCFileServerSync server("0.0.0.0", 9876);
	server.run();
}