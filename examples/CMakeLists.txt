cmake_minimum_required(VERSION 3.16)

add_executable(isdm_demo_pub_sub
    simple_pub_sub/main.cc
)

target_link_libraries(isdm_demo_pub_sub PRIVATE ISDM::core)
target_compile_features(isdm_demo_pub_sub PRIVATE cxx_std_17)
target_compile_options(isdm_demo_pub_sub PRIVATE -Wall -Wextra -Wpedantic)
