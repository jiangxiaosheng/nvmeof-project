cmake_minimum_required(VERSION 3.15)
project(nvmeof)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_BUILD_TYPE Release)
 set(CMAKE_CXX_FLAGS_RELEASE "-O3")

# Find Protobuf installation
# Looks for protobuf-config.cmake file installed by Protobuf's cmake installation.
set(protobuf_MODULE_COMPATIBLE TRUE)
find_package(Protobuf CONFIG REQUIRED)
message(STATUS "Using protobuf ${Protobuf_VERSION}")

set(_PROTOBUF_LIBPROTOBUF protobuf::libprotobuf)
set(_REFLECTION gRPC::grpc++_reflection)
if(CMAKE_CROSSCOMPILING)
  find_program(_PROTOBUF_PROTOC protoc)
else()
  set(_PROTOBUF_PROTOC $<TARGET_FILE:protobuf::protoc>)
endif()

find_package(gRPC CONFIG REQUIRED)
message(STATUS "Using gRPC ${gRPC_VERSION}")

set(_GRPC_GRPCPP gRPC::grpc++)
if(CMAKE_CROSSCOMPILING)
  find_program(_GRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin)
else()
  set(_GRPC_CPP_PLUGIN_EXECUTABLE $<TARGET_FILE:gRPC::grpc_cpp_plugin>)
endif()


set(proto_name net)
# Get proto files
get_filename_component(proto "proto/${proto_name}.proto" ABSOLUTE)
get_filename_component(proto_dir "${proto}" DIRECTORY)

# Generated sources
set(proto_srcs "${CMAKE_CURRENT_BINARY_DIR}/${proto_name}.pb.cc")
set(proto_hdrs "${CMAKE_CURRENT_BINARY_DIR}/${proto_name}.pb.h")
set(grpc_srcs "${CMAKE_CURRENT_BINARY_DIR}/${proto_name}.grpc.pb.cc")
set(grpc_hdrs "${CMAKE_CURRENT_BINARY_DIR}/${proto_name}.grpc.pb.h")
add_custom_command(
    OUTPUT "${proto_srcs}" "${proto_hdrs}" "${grpc_srcs}" "${grpc_hdrs}"
    COMMAND ${_PROTOBUF_PROTOC}
    ARGS --grpc_out "${CMAKE_CURRENT_BINARY_DIR}"
    --cpp_out "${CMAKE_CURRENT_BINARY_DIR}"
    -I "${proto_dir}"
    --plugin=protoc-gen-grpc="${_GRPC_CPP_PLUGIN_EXECUTABLE}"
    "${proto}"
    DEPENDS "${proto}"
)

set(targets "block_target" "block_initiator" "file_target" "file_initiator")

# Include generated *.pb.h files
include_directories(${CMAKE_CURRENT_BINARY_DIR})

add_library(grpc_proto
  ${grpc_srcs}
  ${grpc_hdrs}
  ${proto_srcs}
  ${proto_hdrs})
target_link_libraries(grpc_proto
  ${_REFLECTION}
  ${_GRPC_GRPCPP}
  ${_PROTOBUF_LIBPROTOBUF})

foreach(_target ${targets})
    add_executable(${_target} "${_target}.cc")
    target_link_libraries(${_target} PRIVATE ${_REFLECTION} ${_GRPC_GRPCPP} ${_PROTOBUF_LIBPROTOBUF} grpc_proto ibverbs nvme uring)
    # Cannot do this because these directories are not available during CMake "compile time"
    # target_include_directories(${proto_hdrs} ${grpc_hdrs})
endforeach()

include_directories(third_party)

link_directories(/users/sjiang/nvmeof-project/src/rdma-core/build/lib)

add_executable(block_rw_nvmeof block_rw_nvmeof.cc)
target_link_libraries(block_rw_nvmeof nvme)

add_executable(file_rw_nvmeof file_rw_nvmeof.cc)
target_link_libraries(file_rw_nvmeof nvme uring pthread aio)

add_executable(benchmark benchmark.cc)
target_link_libraries(benchmark pthread uring rdmacm ibverbs)

# target_link_libraries(initiator ibverbs nvme)

# target_link_libraries(target ibverbs nvme)
