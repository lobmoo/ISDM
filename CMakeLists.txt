cmake_minimum_required(VERSION 3.16)
project(ISDM VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ISDM_BUILD_TESTS "Build ISDM tests" OFF)
option(ISDM_BUILD_BENCHMARKS "Build ISDM benchmarks" ON)
option(ISDM_BUILD_EXAMPLES "Build ISDM examples" ON)

find_package(Threads REQUIRED)

# ── Logging dependencies (spdlog + nlohmann/json, header-only) ───────────────
# Local copies under thirdparty/ are preferred (thirdparty/ is not versioned).
# A clean checkout falls back to FetchContent.
include(FetchContent)

set(ISDM_SPDLOG_INCLUDE_DIR "" CACHE PATH "spdlog include directory")
if(NOT ISDM_SPDLOG_INCLUDE_DIR)
    if(EXISTS "${CMAKE_SOURCE_DIR}/thirdparty/spdlog/include/spdlog/spdlog.h")
        set(ISDM_SPDLOG_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/thirdparty/spdlog/include")
    else()
        message(STATUS "ISDM: spdlog not found locally, fetching v1.15.1")
        FetchContent_Declare(isdm_spdlog
            URL https://github.com/gabime/spdlog/archive/refs/tags/v1.15.1.tar.gz)
        FetchContent_GetProperties(isdm_spdlog)
        if(NOT isdm_spdlog_POPULATED)
            FetchContent_Populate(isdm_spdlog)
        endif()
        set(ISDM_SPDLOG_INCLUDE_DIR "${isdm_spdlog_SOURCE_DIR}/include")
    endif()
endif()

set(ISDM_NLOHMANN_INCLUDE_DIR "" CACHE PATH "nlohmann/json include directory")
if(NOT ISDM_NLOHMANN_INCLUDE_DIR)
    if(EXISTS "${CMAKE_SOURCE_DIR}/thirdparty/nlohmann/include/nlohmann/json.hpp")
        set(ISDM_NLOHMANN_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/thirdparty/nlohmann/include")
    else()
        message(STATUS "ISDM: nlohmann/json not found locally, fetching v3.11.3")
        FetchContent_Declare(isdm_nlohmann
            URL https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.tar.gz)
        FetchContent_GetProperties(isdm_nlohmann)
        if(NOT isdm_nlohmann_POPULATED)
            FetchContent_Populate(isdm_nlohmann)
        endif()
        set(ISDM_NLOHMANN_INCLUDE_DIR "${isdm_nlohmann_SOURCE_DIR}/include")
    endif()
endif()

add_subdirectory(src)

if(ISDM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/isdm)
endif()

if(ISDM_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(ISDM_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

install(DIRECTORY src/isdm DESTINATION include
        FILES_MATCHING PATTERN "*.h"
        PATTERN "logger_impl.h" EXCLUDE
        PATTERN "custom_rotating_file_sink.h" EXCLUDE
        PATTERN "custom_rotating_file_sink-inl.h" EXCLUDE)
