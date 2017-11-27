cmake_minimum_required(VERSION 3.8)
project(extfs)
set(CMAKE_C_FLAGS "-Wall -pedantic -Wextra")
add_executable(extfs main.c)
