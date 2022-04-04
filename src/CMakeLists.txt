cmake_minimum_required(VERSION 3.15)
project(nvmeof)

set(CMAKE_CXX_STANDARD 17)

include_directories(third_party)

add_executable(benchmark local_read.cpp)
target_link_libraries(benchmark nvme)

add_executable(initiator initiator.cpp)
target_link_libraries(initiator ibverbs nvme)

add_executable(target target.cpp)
target_link_libraries(target ibverbs nvme)