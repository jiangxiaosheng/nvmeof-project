#include "grpc/grpc.h"
#include "grpcpp/create_channel.h"
#include "net.grpc.pb.h"
#include "net.pb.h"
#include "third_party/argparse.hpp"

#include <chrono>
#include <string>
#include <thread>

using namespace grpc;
using namespace net;
using namespace std;
using namespace chrono;

string server_addr;
int server_port;
size_t buffer_size;
size_t blk_size;
int N;
int queue_depth;
int idle_threshold;
bool large;
bool poll;
bool closed_loop;
bool test_read;
string exprm;
int fn;

void check_target_stats(std::unique_ptr<File::Stub> &stub) {
  StatRequest stat_request;
  StatReply stat_reply;
  ClientContext stat_context;
  stub->GetStat(&stat_context, stat_request, &stat_reply);
  cout << "latency breakdown in the target side:" << endl;
  cout << stat_reply.stat() << endl;
}

void set_target_params(std::unique_ptr<File::Stub> &stub) {
  Parameters params;
  params.set_buffer_size(buffer_size);
  params.set_blk_size(blk_size);
  params.set_queue_depth(queue_depth);
  params.set_poll_threshold(idle_threshold);
  params.set_n(N);

  ClientContext params_context;
  ACK ack;
  Status status = stub->SetParameters(&params_context, params, &ack);
  if (!status.ok()) {
    cerr << "SetParameters rpc failed" << endl;
    return;
  }
}

auto build_rpc_stub() {
  string endpoint = server_addr + ":" + to_string(server_port);
  ChannelArguments channel_args;
  channel_args.SetMaxReceiveMessageSize(-1);
  auto channel = CreateCustomChannel(endpoint, InsecureChannelCredentials(), channel_args);
  auto stub = File::NewStub(channel);
  return stub;
}

void test_large_blocking() {
  auto stub = move(build_rpc_stub());
  set_target_params(stub);

  ClientContext stream_context;
  std::shared_ptr<ClientReaderWriter<FileRequest, FileReply>> stream(stub->AppendOneLargeFile(&stream_context));

  FileReply reply;

  char *buffer = new char[buffer_size];
  memset(buffer, -1, buffer_size);

  FileRequest request;
  request.set_filename("/mnt/large-file");
  request.set_data(buffer);

  auto start = system_clock::now();
  for (int i = 0; i < N; i++) {
    stream->Write(request);
    stream->Read(&reply);
  }
  stream->WritesDone();
  Status status = stream->Finish();
  if (!status.ok()) {
    cerr << "AppendOneLargeFile rpc failed" << endl;
    return;
  }

  auto duration = system_clock::now() - start;
  printf("total time for appending is %lu milliseconds\n", chrono::duration_cast<chrono::milliseconds>(duration).count());
  printf("total data written is %lu bytes\n", N * buffer_size);
  printf("throughput for appending (%ld bytes) is %f MB/s\n", buffer_size, (N * buffer_size) * 1.0 / 1024 / 1024 / chrono::duration_cast<chrono::microseconds>(duration).count() * 1e6);
  printf("IOPS for appending (%ld bytes) is %f\n", buffer_size, N * 1.0 / chrono::duration_cast<chrono::microseconds>(duration).count() * 1e6);

  check_target_stats(stub);
  delete[] buffer;
}

void test_small_blocking() {
  auto stub = move(build_rpc_stub());
  set_target_params(stub);

  ClientContext stream_context;
  std::shared_ptr<ClientReaderWriter<FileRequest, FileReply>> stream(stub->AppendManySmallFiles(&stream_context));

  FileReply reply;

  char *buffer = new char[buffer_size];
  memset(buffer, -1, buffer_size);

  FileRequest request;
  request.set_data(buffer);

  auto start = system_clock::now();
  for (int i = 0; i < N; i++) {
    request.set_filename("/mnt/small-file-" + to_string(i));
    stream->Write(request);
    stream->Read(&reply);
  }
  stream->WritesDone();
  Status status = stream->Finish();
  if (!status.ok()) {
    cerr << "AppendOneLargeFile rpc failed" << endl;
    return;
  }

  auto duration = system_clock::now() - start;
  printf("total time for appending is %lu milliseconds\n", chrono::duration_cast<chrono::milliseconds>(duration).count());
  printf("total data written is %lu bytes\n", N * buffer_size);
  printf("throughput for appending (%ld bytes) is %f MB/s\n", buffer_size, (N * buffer_size) * 1.0 / 1024 / 1024 / chrono::duration_cast<chrono::microseconds>(duration).count() * 1e6);
  printf("IOPS for appending (%ld bytes) is %f\n", buffer_size, N * 1.0 / chrono::duration_cast<chrono::microseconds>(duration).count() * 1e6);

  check_target_stats(stub);
  delete[] buffer;
}

void test_large_async() {
  auto stub = move(build_rpc_stub());
  set_target_params(stub);

  ClientContext stream_context;
  std::shared_ptr<ClientReaderWriter<FileRequest, FileReply>> stream(stub->AppendOneLargeFileAsync(&stream_context));

  FileReply reply;

  char *buffer = new char[buffer_size];
  memset(buffer, -1, buffer_size);

  FileRequest request;
  request.set_filename("/mnt/large-file");
  request.set_data(buffer);

  auto start = system_clock::now();
  for (int i = 0; i < N; i++) {
    stream->Write(request);
  }
  stream->WritesDone();
  stream->Read(&reply);
  Status status = stream->Finish();
  if (!status.ok()) {
    cerr << "AppendOneLargeFile rpc failed" << endl;
    return;
  }

  auto duration = system_clock::now() - start;
  printf("total time for appending is %lu milliseconds\n", chrono::duration_cast<chrono::milliseconds>(duration).count());
  printf("total data written is %lu bytes\n", N * buffer_size);
  printf("throughput for appending (%ld bytes) is %f MB/s\n", buffer_size, (N * buffer_size) * 1.0 / 1024 / 1024 / chrono::duration_cast<chrono::microseconds>(duration).count() * 1e6);
  printf("IOPS for appending (%ld bytes) is %f\n", buffer_size, N * 1.0 / chrono::duration_cast<chrono::microseconds>(duration).count() * 1e6);

  check_target_stats(stub);
  delete[] buffer;
}

void test_large_async_expr() {
  auto stub = move(build_rpc_stub());
  Parameters params;
  params.set_buffer_size(buffer_size);
  params.set_blk_size(blk_size);
  params.set_queue_depth(queue_depth);
  params.set_poll_threshold(idle_threshold);
  params.set_n(N);
  params.set_nfiles(fn);

  {
    ClientContext params_context;
    ACK ack;
    Status status = stub->SetParameters(&params_context, params, &ack);
    if (!status.ok()) {
      cerr << "SetParameters rpc failed" << endl;
      return;
    }
  }

  ClientContext stream_context;
  std::shared_ptr<ClientReaderWriter<FileRequest, FileReply>> stream(stub->AppendOneLargeFileAsyncExpr(&stream_context));

  FileReply reply;

  char *buffer = new char[buffer_size];
  memset(buffer, -1, buffer_size);

  FileRequest request;
  request.set_data(buffer);

  auto start = system_clock::now();
  for (int i = 0; i < N; i++) {
    stream->Write(request);
  }
  stream->WritesDone();
  stream->Read(&reply);
  Status status = stream->Finish();
  if (!status.ok()) {
    cerr << "AppendOneLargeFile rpc failed" << endl;
    return;
  }

  auto duration = system_clock::now() - start;
  printf("total time for appending is %lu milliseconds\n", chrono::duration_cast<chrono::milliseconds>(duration).count());
  printf("total data written is %lu bytes\n", N * buffer_size);
  printf("throughput for appending (%ld bytes) is %f MB/s\n", buffer_size, (N * buffer_size) * 1.0 / 1024 / 1024 / chrono::duration_cast<chrono::microseconds>(duration).count() * 1e6);
  printf("IOPS for appending (%ld bytes) is %f\n", buffer_size, N * 1.0 / chrono::duration_cast<chrono::microseconds>(duration).count() * 1e6);

  check_target_stats(stub);
  delete[] buffer;
}

void test_large_async_multithreads() {
  const int numcpus = std::thread::hardware_concurrency();

  auto stub = move(build_rpc_stub());
  Parameters params;
  params.set_buffer_size(buffer_size);
  params.set_blk_size(blk_size);
  params.set_queue_depth(queue_depth);
  params.set_n(N);
  params.set_nfiles(fn);

  {
    ClientContext params_context;
    ACK ack;
    Status status = stub->SetParameters(&params_context, params, &ack);
    if (!status.ok()) {
      cerr << "SetParameters rpc failed" << endl;
      return;
    }
  }

  auto start = system_clock::now();
  std::thread threads[numcpus];
  for (int i = 0; i < numcpus; i++) {
    threads[i] = std::thread([&]() {
      ClientContext stream_context;
      std::shared_ptr<ClientReaderWriter<FileRequest, FileReply>> stream(stub->AppendOneLargeFileAsyncExpr(&stream_context));

      FileReply reply;

      char *buffer = new char[buffer_size];
      memset(buffer, -1, buffer_size);

      FileRequest request;
      request.set_data(buffer);

      auto start = system_clock::now();
      for (int i = 0; i < N; i++) {
        stream->Write(request);
      }
      stream->WritesDone();
      stream->Read(&reply);
      Status status = stream->Finish();
      if (!status.ok()) {
        cerr << "AppendOneLargeFile rpc failed" << endl;
        return;
      }

      delete[] buffer;
    });
  }

  for (int i = 0; i < numcpus; i++) {
    threads[i].join();
  }

  auto duration = system_clock::now() - start;
  printf("total time for appending is %lu milliseconds\n", chrono::duration_cast<chrono::milliseconds>(duration).count());
  printf("total data written is %lu bytes\n", N * buffer_size);
  printf("throughput for appending (%ld bytes) is %f MB/s\n", buffer_size, (N * buffer_size) * 1.0 / 1024 / 1024 / chrono::duration_cast<chrono::microseconds>(duration).count() * 1e6);
  printf("IOPS for appending (%ld bytes) is %f\n", buffer_size, N * 1.0 / chrono::duration_cast<chrono::microseconds>(duration).count() * 1e6);
}

void test_small_async_multithreads() {
}

void test_small_async() {
  auto stub = move(build_rpc_stub());
  set_target_params(stub);

  ClientContext stream_context;
  std::shared_ptr<ClientReaderWriter<FileRequest, FileReply>> stream(stub->AppendManySmallFilesAsync(&stream_context));

  FileReply reply;

  char *buffer = new char[buffer_size];
  memset(buffer, -1, buffer_size);

  FileRequest request;
  request.set_data(buffer);

  auto start = system_clock::now();
  for (int i = 0; i < N; i++) {
    request.set_filename("/mnt/small-file" + to_string(i));
    stream->Write(request);
  }
  stream->WritesDone();
  stream->Read(&reply);
  Status status = stream->Finish();
  if (!status.ok()) {
    cerr << "AppendOneLargeFile rpc failed" << endl;
    return;
  }

  auto duration = system_clock::now() - start;
  printf("total time for appending is %lu milliseconds\n", chrono::duration_cast<chrono::milliseconds>(duration).count());
  printf("total data written is %lu bytes\n", N * buffer_size);
  printf("throughput for appending (%ld bytes) is %f MB/s\n", buffer_size, (N * buffer_size) * 1.0 / 1024 / 1024 / chrono::duration_cast<chrono::microseconds>(duration).count() * 1e6);
  printf("IOPS for appending (%ld bytes) is %f\n", buffer_size, N * 1.0 / chrono::duration_cast<chrono::microseconds>(duration).count() * 1e6);

  check_target_stats(stub);
  delete[] buffer;
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
  program.add_argument("-addr").help("the ip address of the target").default_value(string("10.10.1.2"));
  program.add_argument("-port").help("the port of the target").default_value(9876).scan<'i', int>();
  program.add_argument("-exp").help("do experiment to help understand results (multiple large files [ml], multi-threads [mt])").default_value("");
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
  server_addr = program.get<string>("-addr");
  server_port = program.get<int>("-port");
  exprm = program.get<string>("-exp");
  fn = program.get<int>("-fn");
}

int main(int argc, char **argv) {
  parse_args(argc, argv);

  if (exprm != "") {
    if (exprm == "ml") {
      cout << "append to more than one large file (async)\n"
           << endl;
      test_large_async_expr();
    } else if (exprm == "mt") {
      cout << "launch per-cpu multiple threads\n"
           << endl;
      if (large) {
        test_large_async_multithreads();
      } else {
        test_small_async_multithreads();
      }
    }

    return 0;
  }

  if (closed_loop) {
    if (large) {
      cout << "append to one large file (blocking)\n"
           << endl;
      test_large_blocking();
    } else {
      cout << "append to " << N << " small files (blocking)\n"
           << endl;
      test_small_blocking();
    }
  } else {
    if (large) {
      cout << "append to one large file (async)\n"
           << endl;
      test_large_async();
    } else {
      cout << "append to " << N << " small files (async)\n"
           << endl;
      test_small_async();
    }
  }
}