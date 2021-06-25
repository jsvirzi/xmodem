cmake_minimum_required(VERSION 3.14)
project(xmodem)

set(CMAKE_CXX_STANDARD 11)
add_link_options(-pthread)

include_directories(include)

add_executable(test-xmodem src/test-xmodem.c src/xmodem.c include/xmodem.h include/ports.h src/ports.c src/stream.c)
add_executable(bert examples/bert.c src/ports.c include/ports.h src/stream.c include/stream.h)
add_executable(test-pattern examples/test-pattern.c src/ports.c include/ports.h src/stream.c include/stream.h)
add_executable(yuyv-lut examples/yuyv-lut.c)
