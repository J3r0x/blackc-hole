cmake_minimum_required(VERSION 4.1)
project(black-hole-simulation)

set(CMAKE_CXX_STANDARD 23)

find_package(raylib CONFIG REQUIRED)


add_executable(black-hole-simulation main.cpp)

target_link_libraries(black-hole-simulation PRIVATE raylib)
