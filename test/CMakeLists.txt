cmake_minimum_required(VERSION 3.3)

find_package(Threads REQUIRED)

add_executable(EchoTest test.cpp)
target_link_libraries(EchoTest Threads::Threads minapp::minapp)