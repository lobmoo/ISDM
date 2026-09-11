cmake_minimum_required(VERSION 3.16)
project(ISDM VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ISDM_BUILD_TESTS "Build ISDM tests" ON)
option(ISDM_BUILD_BENCHMARKS "Build ISDM benchmarks" OFF)

find_package(Threads REQUIRED)

add_subdirectory(src)

if(ISDM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/isdm)
endif()

install(DIRECTORY src/isdm DESTINATION include)
