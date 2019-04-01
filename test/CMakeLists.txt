cmake_minimum_required(VERSION 3.9)
project(EchoTest)

find_package(Threads REQUIRED)

add_executable(${PROJECT_NAME} test.cpp)
target_link_libraries(${PROJECT_NAME} Threads::Threads minapp::minapp)
