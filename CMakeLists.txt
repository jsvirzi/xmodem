cmake_minimum_required(VERSION 3.14)
project(xmodem)

set(CMAKE_CXX_STANDARD 11)
add_link_options(-pthread)

include_directories(include)

add_executable(test-xmodem src/test-xmodem.c src/xmodem.c include/xmodem.h)
