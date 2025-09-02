cmake_minimum_required(VERSION 3.18..4.0)
project(Coro-Roro)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_library(Coro-Roro INTERFACE)

target_include_directories(Coro-Roro INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/ext/include
)

target_link_libraries(Coro-Roro INTERFACE Threads::Threads)

# The Coro-Roro library is now fully header-only

option(ENABLE_TESTS "Build test executable" ON)

# Get CPM.cmake
file(
  DOWNLOAD
  https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.40.8/CPM.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake
  EXPECTED_HASH SHA256=78ba32abdf798bc616bab7c73aac32a17bbd7b06ad9e26a6add69de8f3ae4791
)
include(${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake)

if(ENABLE_TESTS)
    # add_subdirectory(test)
endif()

add_executable(single_file_test
    single_file_test/single_file_test.cpp
)

target_include_directories(single_file_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/ext/include
)

CPMAddPackage("gh:gabime/spdlog@1.15.3")

target_link_libraries(single_file_test PRIVATE spdlog::spdlog_header_only Threads::Threads)

target_compile_options(single_file_test PRIVATE
    /RTC1
    /analyze
    # /Wall
    # /permissive-
)