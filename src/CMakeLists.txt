# ISDM core library.
# Headers and their paired .cc live side-by-side under src/isdm/ (Google style).
add_library(isdm_core STATIC
    isdm/transport/udp_transport.cc
)

target_include_directories(isdm_core
    PUBLIC
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/thirdparty/readerwriterqueue/inlcude
)

target_compile_features(isdm_core PUBLIC cxx_std_17)
target_compile_options(isdm_core PRIVATE -Wall -Wextra -Wpedantic)
find_package(Threads REQUIRED)
target_link_libraries(isdm_core PUBLIC Threads::Threads)

add_library(ISDM::core ALIAS isdm_core)
