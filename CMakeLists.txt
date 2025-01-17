cmake_minimum_required(VERSION 3.20)
project(lab5)

set(CMAKE_CXX_STANDARD 20)

add_executable(main main.cpp sqlite3.c)
add_executable(simulator simulator.cpp)
add_executable(server server.cpp sqlite3.c)

IF (WIN32)
    TARGET_LINK_LIBRARIES(server ws2_32)
ENDIF(WIN32)