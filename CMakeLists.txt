cmake_minimum_required(VERSION 3.20)

project(test)

set(CMAKE_CXX_STANDARD 20)

find_package(Boost REQUIRED COMPONENTS system thread)

add_executable(test main.cpp)

target_link_libraries(test PRIVATE Boost::system Boost::thread)