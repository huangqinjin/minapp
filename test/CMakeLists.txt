cmake_minimum_required(VERSION 3.19)
project(test)

find_package(Threads REQUIRED)

add_executable(echo echo.cpp)
target_link_libraries(echo Threads::Threads minapp::minapp)


add_test(NAME "echo IPV4 loopback" COMMAND echo ipv4)
add_test(NAME "echo IPV6 loopback" COMMAND echo ipv6)
add_test(NAME "echo abstract unix domain socket" COMMAND echo alocal)
