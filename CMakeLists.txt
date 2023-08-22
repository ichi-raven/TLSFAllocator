
cmake_minimum_required(VERSION 3.11)

set(CMAKE_CXX_STANDARD 17)

project(TLSFAllocator)

INCLUDE_DIRECTORIES(
   "${PROJECT_SOURCE_DIR}/include"
)

FILE(GLOB SRCS src/*.cpp)
FILE(GLOB HDRS include/*.hpp)

add_executable(bin ${SRCS})

