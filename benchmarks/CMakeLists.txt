set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)

kota_add_git_dependency(
    benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG v1.9.1
    OPTIONS
        BENCHMARK_ENABLE_TESTING OFF
        BENCHMARK_ENABLE_INSTALL OFF
        BENCHMARK_ENABLE_GTEST_TESTS OFF
        BENCHMARK_ENABLE_WERROR OFF
)

kota_silence_third_party_target(benchmark)
kota_silence_third_party_target(benchmark_main)

add_executable(json_benchmark json_benchmark.cpp)

set_target_properties(json_benchmark PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}"
)

target_include_directories(json_benchmark PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(json_benchmark PRIVATE
    kota::codec::json
    benchmark::benchmark
)

kota_apply_project_options(json_benchmark)
