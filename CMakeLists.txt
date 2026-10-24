# The policy range makes CMake 3.24 and later set the timestamps of the fetched
# archive's files to the extraction time (CMP0135) without an option that CMake
# 3.20 to 3.23 would reject.
cmake_minimum_required(VERSION 3.20...3.28)
project(ParallelTarjan VERSION 0.2.0 LANGUAGES CXX)

include(CTest)
include(FetchContent)
find_package(Threads REQUIRED)

option(PTARJAN_BUILD_BENCHMARKS "Build the ptarjan_bench benchmark tool" ON)
option(PTARJAN_WARNINGS_AS_ERRORS "Treat compiler warnings in this project as errors" OFF)
set(PTARJAN_SANITIZER "none" CACHE STRING "Sanitizer: none, address, or thread")
set_property(CACHE PTARJAN_SANITIZER PROPERTY STRINGS none address thread)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # for clang-tidy

if(NOT PTARJAN_SANITIZER STREQUAL "none")
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "The sanitizer presets require GCC or Clang.")
    endif()
    if(PTARJAN_SANITIZER STREQUAL "address")
        set(PTARJAN_SANITIZER_FLAGS -fsanitize=address,undefined -fno-omit-frame-pointer)
    elseif(PTARJAN_SANITIZER STREQUAL "thread")
        set(PTARJAN_SANITIZER_FLAGS -fsanitize=thread -fno-omit-frame-pointer)
    else()
        message(FATAL_ERROR "Unknown PTARJAN_SANITIZER=${PTARJAN_SANITIZER}")
    endif()
    add_compile_options(${PTARJAN_SANITIZER_FLAGS})
    add_link_options(${PTARJAN_SANITIZER_FLAGS})
endif()

# moodycamel::ConcurrentQueue (header-only) holds the searches waiting to be
# resumed. It is fetched at a pinned release; to build offline, point
# FETCHCONTENT_SOURCE_DIR_CONCURRENTQUEUE at a local checkout of that tag.
FetchContent_Declare(concurrentqueue
    URL https://github.com/cameron314/concurrentqueue/archive/refs/tags/v1.0.4.tar.gz
    URL_HASH SHA256=87fbc9884d60d0d4bf3462c18f4c0ee0a9311d0519341cac7cbd361c885e5281
    # Only the header is needed; skip the upstream build and install rules.
    SOURCE_SUBDIR header-only
)
FetchContent_MakeAvailable(concurrentqueue)
add_library(ptarjan_concurrentqueue INTERFACE)
target_include_directories(ptarjan_concurrentqueue SYSTEM INTERFACE "${concurrentqueue_SOURCE_DIR}")

# Warnings for this project's own targets only.
add_library(ptarjan_warnings INTERFACE)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ptarjan_warnings INTERFACE -Wall -Wextra -Wpedantic -Wshadow -Wconversion
        -Wno-sign-conversion $<$<BOOL:${PTARJAN_WARNINGS_AS_ERRORS}>:-Werror>)
elseif(MSVC)
    target_compile_options(ptarjan_warnings INTERFACE /W4 $<$<BOOL:${PTARJAN_WARNINGS_AS_ERRORS}>:/WX>)
endif()

add_library(ptarjan
    src/components.cpp
    src/graph.cpp
    src/hybrid.cpp
    src/sequential_tarjan.cpp
    src/parallel/parallel_tarjan.cpp
    src/parallel/search.cpp
    src/parallel/suspension.cpp
    src/parallel/worker.cpp
)
add_library(ptarjan::ptarjan ALIAS ptarjan)
target_include_directories(ptarjan
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    PRIVATE src src/parallel)
target_compile_features(ptarjan PUBLIC cxx_std_20)
target_link_libraries(ptarjan
    PUBLIC Threads::Threads
    PRIVATE ptarjan_concurrentqueue ptarjan_warnings)

add_executable(ptarjan_demo examples/scc.cpp)
target_link_libraries(ptarjan_demo PRIVATE ptarjan ptarjan_warnings)

if(PTARJAN_BUILD_BENCHMARKS)
    add_executable(ptarjan_bench bench/ptarjan_bench.cpp bench/graph_sources.cpp)
    target_link_libraries(ptarjan_bench PRIVATE ptarjan ptarjan_warnings)
endif()

if(BUILD_TESTING)
    # The tests drive the parallel scheduler directly, so they see its internal headers.
    add_library(ptarjan_internals INTERFACE)
    target_include_directories(ptarjan_internals INTERFACE src src/parallel)
    target_link_libraries(ptarjan_internals INTERFACE ptarjan ptarjan_concurrentqueue)

    add_executable(ptarjan_tests tests/scc_tests.cpp)
    target_link_libraries(ptarjan_tests PRIVATE ptarjan_internals ptarjan_warnings)
    foreach(test_case api differential thread-boundaries contention contention-locked termination fan-in hybrid)
        add_test(NAME ${test_case} COMMAND ptarjan_tests --case ${test_case})
        set_tests_properties(${test_case} PROPERTIES TIMEOUT 120)
    endforeach()
    set_tests_properties(contention contention-locked termination fan-in hybrid PROPERTIES LABELS "stress")

    add_executable(blocked_list_tests tests/blocked_list.cpp)
    target_link_libraries(blocked_list_tests PRIVATE ptarjan_internals ptarjan_warnings)
    add_test(NAME blocked-list COMMAND blocked_list_tests)
    set_tests_properties(blocked-list PROPERTIES TIMEOUT 60)

    # GCC's ThreadSanitizer does not model the fences in the moodycamel queue.
    # These tests use the mutex-backed pending queue or no queue at all.
    set_property(TEST contention-locked blocked-list APPEND PROPERTY LABELS tsan-core)
endif()
