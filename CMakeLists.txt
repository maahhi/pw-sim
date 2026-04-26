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

# toml++ (header-only TOML parser — used by config/TomlLoader.cpp)
include(FetchContent)
FetchContent_Declare(
  tomlplusplus
  GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
  GIT_TAG        v3.4.0
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(tomlplusplus)

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

# RTNeural static library
set(RTNEURAL_ROOT ${CMAKE_SOURCE_DIR}/third_party/RTNeural)
add_library(RTNeural STATIC
  ${RTNEURAL_ROOT}/RTNeural/RTNeural.cpp
)
set_property(TARGET RTNeural PROPERTY CXX_STANDARD 17)
target_include_directories(RTNeural
  PUBLIC
    ${RTNEURAL_ROOT}                     # consumers: #include "RTNeural/RTNeural.h"
    ${RTNEURAL_ROOT}/modules/Eigen       # <Eigen/Dense>
  PRIVATE
    ${RTNEURAL_ROOT}/RTNeural            # internal headers
)
target_compile_definitions(RTNeural PUBLIC
  RTNEURAL_NAMESPACE=RTNeural
  RTNEURAL_USE_EIGEN=1
  RTNEURAL_DEFAULT_ALIGNMENT=16
)
target_compile_options(RTNeural PRIVATE -w)

# NeuralAmpModelerCore (NAM) static library
set(NAM_ROOT ${CMAKE_SOURCE_DIR}/third_party/NeuralAmpModelerCore)
add_library(nam STATIC
  ${NAM_ROOT}/NAM/activations.cpp
  ${NAM_ROOT}/NAM/container.cpp
  ${NAM_ROOT}/NAM/conv1d.cpp
  ${NAM_ROOT}/NAM/convnet.cpp
  ${NAM_ROOT}/NAM/dsp.cpp
  ${NAM_ROOT}/NAM/get_dsp.cpp
  ${NAM_ROOT}/NAM/lstm.cpp
  ${NAM_ROOT}/NAM/ring_buffer.cpp
  ${NAM_ROOT}/NAM/util.cpp
  ${NAM_ROOT}/NAM/wavenet/a2_fast.cpp
  ${NAM_ROOT}/NAM/wavenet/model.cpp
  ${NAM_ROOT}/NAM/wavenet/slimmable.cpp
)
set_property(TARGET nam PROPERTY CXX_STANDARD 20)
target_include_directories(nam PUBLIC
  ${NAM_ROOT}/NAM                          # NAM public headers
  ${NAM_ROOT}/Dependencies/nlohmann        # json.hpp
  ${RTNEURAL_ROOT}/modules/Eigen           # <Eigen/Dense>
)
target_compile_definitions(nam PUBLIC NAM_ENABLE_A2_FAST)
target_compile_options(nam PRIVATE -w)

# Find libsndfile
find_library(SNDFILE_LIB sndfile REQUIRED)
find_path(SNDFILE_INCLUDE sndfile.h REQUIRED)

# Source files
set(SOURCES
  src/main.cpp
  src/config/SimConfig.cpp
  src/config/TomlLoader.cpp
  src/config/CliParser.cpp
  src/io/AudioReader.cpp
  src/io/AudioWriter.cpp
  src/engine/SimEngine.cpp
  src/engine/VirtualClock.cpp
  src/engine/XrunPolicy.cpp
  src/engine/ProbeReader.cpp
  src/metrics/MetricsWriter.cpp
)

add_executable(pw-sim ${SOURCES})

target_include_directories(pw-sim
  PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${SNDFILE_INCLUDE}
  SYSTEM PRIVATE
    ${NAM_ROOT}/NAM                    # NamFut.hpp → NAM headers (third-party, no warnings)
    ${NAM_ROOT}/Dependencies/nlohmann  # json.hpp
    ${RTNEURAL_ROOT}/modules/Eigen     # <Eigen/Dense>
)

# Force-load libnam so all static ConfigParserHelper registrations actually run.
# Without this, the linker strips unused TUs and LSTM/ConvNet/WaveNet parsers
# are never registered, causing a runtime error on those model architectures.
if(APPLE)
  target_link_options(pw-sim PRIVATE "LINKER:-force_load,$<TARGET_FILE:nam>")
else()
  target_link_options(pw-sim PRIVATE
    "LINKER:--whole-archive"
    $<TARGET_FILE:nam>
    "LINKER:--no-whole-archive"
  )
endif()

target_link_libraries(pw-sim PRIVATE
  tomlplusplus::tomlplusplus
  ${SNDFILE_LIB}
  rnnoise
  RTNeural
  nam
  pthread
  m
)

# Install
install(TARGETS pw-sim DESTINATION bin)

message(STATUS "Build type  : ${CMAKE_BUILD_TYPE}")
message(STATUS "libsndfile  : ${SNDFILE_LIB}")
