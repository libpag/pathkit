cmake_minimum_required(VERSION 3.5)
project(PathKit)

set(CMAKE_CXX_STANDARD 17)

file(GLOB_RECURSE SOURCE_FILES src/*.cpp)

add_library(pathkit STATIC ${SOURCE_FILES})
include_directories(./)
add_executable(PathKitDemo main.cpp)
target_link_libraries(PathKitDemo pathkit)

# Off by default so that projects vendoring pathkit do not build its tests. The tests link the
# static library the same way the demo does; they live outside src/ so the GLOB above never picks
# them into the library itself.
option(PATHKIT_BUILD_TESTS "Build the PathKit regression tests" OFF)
if (PATHKIT_BUILD_TESTS)
  add_executable(PathKitTests tests/AsWindingTest.cpp)
  target_link_libraries(PathKitTests pathkit)
  enable_testing()
  add_test(NAME PathKitTests COMMAND PathKitTests)
endif ()
