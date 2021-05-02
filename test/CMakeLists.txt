cmake_minimum_required(VERSION 3.9)
project(test)

find_package(Threads REQUIRED)

add_executable(echo echo.cpp)
target_link_libraries(echo Threads::Threads minapp::minapp)
