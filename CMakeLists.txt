cmake_minimum_required(VERSION 3.15)
project(ISDM VERSION 0.1.0 LANGUAGES CXX)

# C++17标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译选项
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG -march=native")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0 -fsanitize=address -fsanitize=undefined")
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} -fsanitize=address -fsanitize=undefined")
endif()

# 选项
option(ISDM_BUILD_TESTS "Build tests" ON)
option(ISDM_BUILD_BENCHMARKS "Build benchmarks" ON)
option(ISDM_BUILD_EXAMPLES "Build examples" ON)

# 输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# 查找依赖
find_package(Threads REQUIRED)

# 第三方库（如果存在）
if(EXISTS ${CMAKE_SOURCE_DIR}/third_party/googletest)
    add_subdirectory(third_party/googletest EXCLUDE_FROM_ALL)
endif()

if(EXISTS ${CMAKE_SOURCE_DIR}/third_party/benchmark)
    add_subdirectory(third_party/benchmark EXCLUDE_FROM_ALL)
endif()

if(EXISTS ${CMAKE_SOURCE_DIR}/third_party/spdlog)
    add_subdirectory(third_party/spdlog EXCLUDE_FROM_ALL)
endif()

# 核心库源文件
set(ISDM_CORE_SOURCES
    src/core/type/primitive_type.cc
    src/core/type/type_descriptor.cc
    src/core/schema/schema_registry.cc
    src/core/object/object_store.cc
    src/core/api/node.cc
    src/core/api/publisher.cc
    src/core/api/subscriber.cc
    src/runtime/memory/memory_pool.cc
    src/runtime/memory/buffer_pool.cc
    src/runtime/batch/batch_aggregator.cc
    src/runtime/delta/delta_engine.cc
    src/protocol/control/discovery.cc
    src/protocol/data/frame_builder.cc
    src/protocol/data/frame_parser.cc
    src/protocol/codec/encoder.cc
    src/protocol/codec/decoder.cc
    src/transport/udp/udp_transport.cc
    src/transport/shm/shm_transport.cc
    src/util/logger.cc
)

# 创建核心库
add_library(isdm_core STATIC ${ISDM_CORE_SOURCES})

target_include_directories(isdm_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(isdm_core
    PUBLIC
        Threads::Threads
    PRIVATE
        $<$<TARGET_EXISTS:spdlog::spdlog>:spdlog::spdlog>
)

# 编译定义
target_compile_definitions(isdm_core
    PRIVATE
        ISDM_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        ISDM_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        ISDM_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

# 测试
if(ISDM_BUILD_TESTS AND TARGET gtest)
    enable_testing()
    add_subdirectory(tests)
endif()

# 性能测试
if(ISDM_BUILD_BENCHMARKS AND TARGET benchmark::benchmark)
    add_subdirectory(benchmarks)
endif()

# 示例
if(ISDM_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# 安装规则
install(TARGETS isdm_core
    EXPORT isdm-targets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/isdm
    DESTINATION include
)

install(EXPORT isdm-targets
    FILE isdm-targets.cmake
    NAMESPACE isdm::
    DESTINATION lib/cmake/isdm
)

# 生成配置文件
include(CMakePackageConfigHelpers)
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/isdm-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/isdm-config.cmake
    INSTALL_DESTINATION lib/cmake/isdm
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/isdm-config.cmake
    DESTINATION lib/cmake/isdm
)

# 打印配置信息
message(STATUS "ISDM Configuration:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Build Tests: ${ISDM_BUILD_TESTS}")
message(STATUS "  Build Benchmarks: ${ISDM_BUILD_BENCHMARKS}")
message(STATUS "  Build Examples: ${ISDM_BUILD_EXAMPLES}")
