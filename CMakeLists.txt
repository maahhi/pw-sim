cmake_minimum_required(VERSION 3.16)
project(pw-sim VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build type default
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags
add_compile_options(
  -Wall
  -Wextra
  -Wpedantic
  $<$<CONFIG:Release>:-O2>
  $<$<CONFIG:Debug>:-g>
  $<$<CONFIG:Debug>:-O0>
  $<$<CONFIG:Debug>:-fsanitize=address>
)

add_link_options(
  $<$<CONFIG:Debug>:-fsanitize=address>
)

# Find libsndfile
find_library(SNDFILE_LIB sndfile REQUIRED)
find_path(SNDFILE_INCLUDE sndfile.h REQUIRED)

# Source files
set(SOURCES
  src/main.cpp
  src/io/AudioReader.cpp
  src/io/AudioWriter.cpp
  src/engine/SimEngine.cpp
  src/engine/VirtualClock.cpp
  src/engine/XrunPolicy.cpp
  src/engine/ProbeReader.cpp
  src/metrics/MetricsWriter.cpp
)

add_executable(pw-sim ${SOURCES})

target_include_directories(pw-sim PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${SNDFILE_INCLUDE}
)

target_link_libraries(pw-sim PRIVATE
  ${SNDFILE_LIB}
  pthread
)

# Install
install(TARGETS pw-sim DESTINATION bin)

message(STATUS "Build type  : ${CMAKE_BUILD_TYPE}")
message(STATUS "libsndfile  : ${SNDFILE_LIB}")
