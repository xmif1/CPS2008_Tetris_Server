cmake_minimum_required(VERSION 3.17)
project(CPS2008_Tetris_Server C)

set(CMAKE_C_STANDARD 99)

add_executable(CPS2008_Tetris_Server server.c server.h)

target_link_libraries(CPS2008_Tetris_Server pthread m)
