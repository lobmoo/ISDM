cmake_minimum_required(VERSION 3.16)

add_executable(isdm_demo_pub_sub
    simple_pub_sub/main.cc
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
    events/main.cc
)

target_link_libraries(isdm_demo_events PRIVATE ISDM::core)
target_compile_features(isdm_demo_events PRIVATE cxx_std_17)
target_compile_options(isdm_demo_events PRIVATE -Wall -Wextra -Wpedantic)

# Two-process demo: independent publisher and subscriber executables.
add_executable(isdm_publisher two_process/publisher.cc)
target_link_libraries(isdm_publisher PRIVATE ISDM::core)
target_compile_features(isdm_publisher PRIVATE cxx_std_17)
target_compile_options(isdm_publisher PRIVATE -Wall -Wextra -Wpedantic)

add_executable(isdm_subscriber two_process/subscriber.cc)
target_link_libraries(isdm_subscriber PRIVATE ISDM::core)
target_compile_features(isdm_subscriber PRIVATE cxx_std_17)
target_compile_options(isdm_subscriber PRIVATE -Wall -Wextra -Wpedantic)
