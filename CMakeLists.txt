cmake_minimum_required(VERSION 3.5)
project(PathKit)

set(CMAKE_CXX_STANDARD 17)

file(GLOB_RECURSE SOURCE_FILES src/*.cpp)

add_library(pathkit STATIC ${SOURCE_FILES})
include_directories(./)
add_executable(PathKitDemo main.cpp)
target_link_libraries(PathKitDemo pathkit)
