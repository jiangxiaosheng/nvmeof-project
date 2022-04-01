cmake_minimum_required(VERSION 3.19)
project(benchmark)

set(CMAKE_CXX_STANDARD 17)

add_executable(benchmark local_read.cpp)
target_link_libraries(benchmark nvme)