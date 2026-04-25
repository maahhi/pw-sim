cmake_minimum_required(VERSION 3.16)
project(pw-sim VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build type default
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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

# RNNoise static library (third_party/rnnoise)
set(RNNOISE_SRC ${CMAKE_SOURCE_DIR}/third_party/rnnoise/src)
add_library(rnnoise STATIC
  ${RNNOISE_SRC}/celt_lpc.c
  ${RNNOISE_SRC}/denoise.c
  ${RNNOISE_SRC}/kiss_fft.c
  ${RNNOISE_SRC}/pitch.c
  ${RNNOISE_SRC}/rnn.c
  ${RNNOISE_SRC}/rnn_data.c
  ${RNNOISE_SRC}/rnn_reader.c
)
target_include_directories(rnnoise PUBLIC
  ${CMAKE_SOURCE_DIR}/third_party/rnnoise/include
  ${RNNOISE_SRC}
)
target_compile_options(rnnoise PRIVATE -w)  # third-party: suppress all warnings

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
  rnnoise
  pthread
  m
)

# Install
install(TARGETS pw-sim DESTINATION bin)

message(STATUS "Build type  : ${CMAKE_BUILD_TYPE}")
message(STATUS "libsndfile  : ${SNDFILE_LIB}")
