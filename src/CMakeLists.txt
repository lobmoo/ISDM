# ISDM core library.
# Headers and their paired .cc live side-by-side under src/isdm/ (Google style).
add_library(isdm_core STATIC
    isdm/codec/object_codec.cc
    isdm/discovery/discovery_manager.cc
    isdm/discovery/peer_table.cc
    isdm/log/logger.cc
    isdm/log/logger_impl.cc
    isdm/node.cc
    isdm/protocol/batch_data_message.cc
    isdm/protocol/command.cc
    isdm/protocol/crc32c.cc
    isdm/protocol/data_message.cc
    isdm/protocol/delta.cc
    isdm/protocol/discovery.cc
    isdm/protocol/event.cc
    isdm/protocol/fragment.cc
    isdm/protocol/frame.cc
    isdm/protocol/schema_sync.cc
    isdm/protocol/schema_wire.cc
    isdm/protocol/snapshot.cc
    isdm/runtime/event_store.cc
    isdm/runtime/object_state_store.cc
    isdm/schema/schema_registry.cc
    isdm/transport/endpoint.cc
    isdm/transport/udp_transport.cc
)

target_include_directories(isdm_core
    PUBLIC
        ${CMAKE_SOURCE_DIR}/src
)
target_include_directories(isdm_core SYSTEM PRIVATE
    ${ISDM_SPDLOG_INCLUDE_DIR}
    ${ISDM_NLOHMANN_INCLUDE_DIR}
)

target_compile_features(isdm_core PUBLIC cxx_std_17)
target_compile_options(isdm_core PRIVATE -Wall -Wextra -Wpedantic)
find_package(Threads REQUIRED)
target_link_libraries(isdm_core PUBLIC Threads::Threads)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 10)
    target_link_libraries(isdm_core PRIVATE stdc++fs)
endif()

add_library(ISDM::core ALIAS isdm_core)
