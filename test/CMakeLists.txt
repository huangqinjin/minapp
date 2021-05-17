cmake_minimum_required(VERSION 3.19)
project(test)

find_package(Threads REQUIRED)
link_libraries(Threads::Threads minapp::minapp)

add_executable(echo echo.cpp)
add_executable(forward forward.cpp)

add_test(NAME "echo IPV4 loopback" COMMAND echo ipv4)
add_test(NAME "echo IPV6 loopback" COMMAND echo ipv6)
add_test(NAME "echo abstract unix domain socket" COMMAND echo alocal)

add_test(NAME "echo with forward" COMMAND ${CMAKE_COMMAND} -DCMD=execute_process -P ${CMAKE_CURRENT_LIST_DIR}/cmd.cmake --
    | $<TARGET_FILE:echo> [::]:2333 :1 alocal
    | $<TARGET_FILE:forward> :1 [::1]:2333 alocal
)
