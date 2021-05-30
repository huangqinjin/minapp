cmake_minimum_required(VERSION 3.19)
project(test)

find_package(Threads REQUIRED)
link_libraries(Threads::Threads minapp::minapp)

add_executable(echo echo.cpp)
add_executable(forward forward.cpp)
add_executable(socks5 socks5.cpp)

add_test(NAME "echo IPV4 loopback" COMMAND echo ipv4)
add_test(NAME "echo IPV6 loopback" COMMAND echo ipv6)
add_test(NAME "echo abstract unix domain socket" COMMAND echo alocal)

add_test(NAME "echo with forward" COMMAND ${CMAKE_COMMAND} -DCMD=execute_process -P ${CMAKE_CURRENT_LIST_DIR}/cmd.cmake --
    | $<TARGET_FILE:echo> [::]:2333 :1 alocal
    | $<TARGET_FILE:forward> :1 [::1]:2333 alocal
)

add_test(NAME "sock5 download http" COMMAND ${CMAKE_COMMAND} -DCMD=execute_process -P ${CMAKE_CURRENT_LIST_DIR}/cmd.cmake --
    | ${CMAKE_COMMAND} -E env http_proxy=socks5://localhost:11080
      ${CMAKE_COMMAND} -DCMD=file -P ${CMAKE_CURRENT_LIST_DIR}/cmd.cmake -- DOWNLOAD http://cmake.org
    | $<TARGET_FILE:socks5> :11080 ipv4
)
add_test(NAME "sock5 download https" COMMAND ${CMAKE_COMMAND} -DCMD=execute_process -P ${CMAKE_CURRENT_LIST_DIR}/cmd.cmake --
    | ${CMAKE_COMMAND} -E env https_proxy=socks5://localhost:11080
      ${CMAKE_COMMAND} -DCMD=file -P ${CMAKE_CURRENT_LIST_DIR}/cmd.cmake -- DOWNLOAD https://cmake.org
    | $<TARGET_FILE:socks5> :11080 ipv4
)

find_package(Git)
if(Git_FOUND)
    add_test(NAME "sock5 git" COMMAND ${CMAKE_COMMAND} -DCMD=execute_process -P ${CMAKE_CURRENT_LIST_DIR}/cmd.cmake --
        | ${GIT_EXECUTABLE} clone https://github.com/huangqinjin/minapp.git -c http.proxy=socks5h://localhost:11080
        | $<TARGET_FILE:socks5> :11080 ipv4
    )
endif()

find_program(CURL_EXECUTABLE curl)
if(CURL_EXECUTABLE)
    add_test(NAME "sock5 curl proxy" COMMAND ${CMAKE_COMMAND} -DCMD=execute_process -P ${CMAKE_CURRENT_LIST_DIR}/cmd.cmake --
        | ${CURL_EXECUTABLE} -x socks5h://user:pass@localhost:11080 -I https://nginx.org
        | $<TARGET_FILE:socks5> :11080
    )
    add_test(NAME "sock5 curl http" COMMAND ${CMAKE_COMMAND} -DCMD=execute_process -P ${CMAKE_CURRENT_LIST_DIR}/cmd.cmake --
        | ${CMAKE_COMMAND} -E env http_proxy=socks5://localhost:11080 ${CURL_EXECUTABLE} -I http://nginx.org
        | $<TARGET_FILE:socks5> :11080
    )
    add_test(NAME "sock5 curl https" COMMAND ${CMAKE_COMMAND} -DCMD=execute_process -P ${CMAKE_CURRENT_LIST_DIR}/cmd.cmake --
        | ${CMAKE_COMMAND} -E env https_proxy=socks5://localhost:11080 ${CURL_EXECUTABLE} -I https://nginx.org
        | $<TARGET_FILE:socks5> :11080
    )
endif()
