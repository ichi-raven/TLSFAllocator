
cmake_minimum_required(VERSION 3.11)


project(FreeC++Writing)

add_definitions("-w -g -O0 -std=c++17")

INCLUDE_DIRECTORIES(
   "${PROJECT_SOURCE_DIR}/include"
)

FILE(GLOB SRCS src/*.cpp)
FILE(GLOB HDRS include/*.hpp)

add_executable(bin ${SRCS})

