add_executable(isdm_bench bench_main.cc)
target_link_libraries(isdm_bench PRIVATE ISDM::core)
target_compile_features(isdm_bench PRIVATE cxx_std_17)
target_compile_options(isdm_bench PRIVATE -O2 -Wall -Wextra)