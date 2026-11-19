cmake_minimum_required(VERSION 3.16)

add_executable(isdm_demo_pub_sub
    basic_pub_sub/main.cc
)

target_link_libraries(isdm_demo_pub_sub PRIVATE ISDM::core)
target_compile_features(isdm_demo_pub_sub PRIVATE cxx_std_17)
target_compile_options(isdm_demo_pub_sub PRIVATE -Wall -Wextra -Wpedantic)

# Complex types: nested struct, fixed array, sequence, and string.
add_executable(isdm_demo_complex_types
    complex_types/main.cc
)

target_link_libraries(isdm_demo_complex_types PRIVATE ISDM::core)
target_compile_features(isdm_demo_complex_types PRIVATE cxx_std_17)
target_compile_options(isdm_demo_complex_types PRIVATE -Wall -Wextra -Wpedantic)

# Type discovery: subscriber uses runtime schema only (no business structs).
add_executable(isdm_demo_type_discovery
    type_discovery/main.cc
)

target_link_libraries(isdm_demo_type_discovery PRIVATE ISDM::core)
target_compile_features(isdm_demo_type_discovery PRIVATE cxx_std_17)
target_compile_options(isdm_demo_type_discovery PRIVATE -Wall -Wextra -Wpedantic)

# Events: late subscriber replay + live events + watermark.
add_executable(isdm_demo_events
    event_replay/main.cc
)

target_link_libraries(isdm_demo_events PRIVATE ISDM::core)
target_compile_features(isdm_demo_events PRIVATE cxx_std_17)
target_compile_options(isdm_demo_events PRIVATE -Wall -Wextra -Wpedantic)

# Commands: directed command with ACK/NACK and retry/timeout.
add_executable(isdm_demo_commands
    command_ack/main.cc
)

target_link_libraries(isdm_demo_commands PRIVATE ISDM::core)
target_compile_features(isdm_demo_commands PRIVATE cxx_std_17)
target_compile_options(isdm_demo_commands PRIVATE -Wall -Wextra -Wpedantic)

# Autonomous subscriber: scan models/objects, then subscribe by name.
add_executable(isdm_demo_autonomous_subscriber
    autonomous_scan/main.cc
)

target_link_libraries(isdm_demo_autonomous_subscriber PRIVATE ISDM::core)
target_compile_features(isdm_demo_autonomous_subscriber PRIVATE cxx_std_17)
target_compile_options(isdm_demo_autonomous_subscriber PRIVATE -Wall -Wextra -Wpedantic)

# Two-process ID demo: one executable, pub/sub roles, TypeId-based APIs.
add_executable(isdm_demo_two_process type_id_pub_sub/main.cc)
target_link_libraries(isdm_demo_two_process PRIVATE ISDM::core)
target_compile_features(isdm_demo_two_process PRIVATE cxx_std_17)
target_compile_options(isdm_demo_two_process PRIVATE -Wall -Wextra -Wpedantic)
