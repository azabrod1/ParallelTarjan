cmake_minimum_required(VERSION 3.20)
project(ParallelTarjan VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
include(CheckCXXSourceCompiles)
include(FetchContent)
find_package(Threads REQUIRED)

# moodycamel::ConcurrentQueue (header-only) holds the searches waiting to be
# resumed. It is fetched at a pinned release; to build offline, point
# FETCHCONTENT_SOURCE_DIR_CONCURRENTQUEUE at a local checkout of that tag.
FetchContent_Declare(concurrentqueue
    URL https://github.com/cameron314/concurrentqueue/archive/refs/tags/v1.0.4.tar.gz
    URL_HASH SHA256=87fbc9884d60d0d4bf3462c18f4c0ee0a9311d0519341cac7cbd361c885e5281
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    # Only the header is needed; skip the upstream build and install rules.
    SOURCE_SUBDIR header-only
)
FetchContent_MakeAvailable(concurrentqueue)
add_library(concurrentqueue INTERFACE)
target_include_directories(concurrentqueue SYSTEM INTERFACE "${concurrentqueue_SOURCE_DIR}")

option(PARALLEL_TARJAN_ENABLE_TBB "Enable the optional oneTBB dictionary" OFF)
option(PARALLEL_TARJAN_BUILD_LEGACY_BENCHMARK "Build the historical benchmark with local dataset paths" OFF)
set(PARALLEL_TARJAN_SANITIZER "none" CACHE STRING "Sanitizer: none, address, or thread")
set_property(CACHE PARALLEL_TARJAN_SANITIZER PROPERTY STRINGS none address thread)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(parallel_tarjan
    "Tarjan Code/Reference.cpp"
    "Tarjan Code/multiThreadedTarjan.cpp"
    "Tarjan Code/search.cpp"
    "Tarjan Code/singleThreadedTarjan.cpp"
    "Tarjan Code/stealingQueue.cpp"
    "Tarjan Code/suspensionManager.cpp"
    "Tarjan Code/typedefs.cpp"
    "Tarjan Code/utilities.cpp"
    "Tarjan Code/worker.cpp"
)
target_include_directories(parallel_tarjan PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Tarjan Code")
target_compile_features(parallel_tarjan PUBLIC cxx_std_17)
target_link_libraries(parallel_tarjan PUBLIC Threads::Threads concurrentqueue)

add_executable(parallel_tarjan_demo examples/scc.cpp)
target_link_libraries(parallel_tarjan_demo PRIVATE parallel_tarjan)

# The experimental open-addressed dictionaries use 16-byte atomics. GCC on
# Linux supplies these through libatomic; other platforms may provide them
# directly. Do not claim they are lock-free merely because they are atomic.
set(PARALLEL_TARJAN_ATOMIC_PROBE "
    #include <atomic>
    struct Entry { unsigned key; unsigned age; void* pointer; };
    int main() { std::atomic<Entry> entry{}; return entry.load().key; }
")
check_cxx_source_compiles("${PARALLEL_TARJAN_ATOMIC_PROBE}" PARALLEL_TARJAN_ATOMICS_BUILTIN)
if(NOT PARALLEL_TARJAN_ATOMICS_BUILTIN)
    set(CMAKE_REQUIRED_LIBRARIES atomic)
    check_cxx_source_compiles("${PARALLEL_TARJAN_ATOMIC_PROBE}" PARALLEL_TARJAN_ATOMICS_LIBRARY)
    unset(CMAKE_REQUIRED_LIBRARIES)
    if(NOT PARALLEL_TARJAN_ATOMICS_LIBRARY)
        message(FATAL_ERROR "The compiler needs support for 16-byte std::atomic operations (usually libatomic).")
    endif()
    target_link_libraries(parallel_tarjan PUBLIC atomic)
endif()

if(PARALLEL_TARJAN_ENABLE_TBB)
    find_package(TBB REQUIRED COMPONENTS tbb)
    target_compile_definitions(parallel_tarjan PUBLIC PARALLEL_TARJAN_ENABLE_TBB=1)
    target_link_libraries(parallel_tarjan PUBLIC TBB::tbb)
endif()

if(NOT PARALLEL_TARJAN_SANITIZER STREQUAL "none")
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "The sanitizer presets currently require GCC or Clang.")
    endif()
    if(PARALLEL_TARJAN_SANITIZER STREQUAL "address")
        set(PARALLEL_TARJAN_SANITIZER_FLAGS -fsanitize=address,undefined -fno-omit-frame-pointer)
    elseif(PARALLEL_TARJAN_SANITIZER STREQUAL "thread")
        set(PARALLEL_TARJAN_SANITIZER_FLAGS -fsanitize=thread -fno-omit-frame-pointer)
    else()
        message(FATAL_ERROR "Unknown PARALLEL_TARJAN_SANITIZER=${PARALLEL_TARJAN_SANITIZER}")
    endif()
    target_compile_options(parallel_tarjan PUBLIC ${PARALLEL_TARJAN_SANITIZER_FLAGS})
    target_link_options(parallel_tarjan PUBLIC ${PARALLEL_TARJAN_SANITIZER_FLAGS})
endif()

if(PARALLEL_TARJAN_BUILD_LEGACY_BENCHMARK)
    add_executable(parallel_tarjan_legacy "Tarjan Code/main.cpp")
    target_link_libraries(parallel_tarjan_legacy PRIVATE parallel_tarjan)
endif()

if(BUILD_TESTING)
    add_executable(parallel_tarjan_tests tests/regression.cpp)
    target_link_libraries(parallel_tarjan_tests PRIVATE parallel_tarjan)
    foreach(test_case graph differential thread-boundaries contention contention-locked termination)
        add_test(NAME ${test_case} COMMAND parallel_tarjan_tests --case ${test_case})
        set_tests_properties(${test_case} PROPERTIES TIMEOUT 60)
    endforeach()
    set_tests_properties(contention contention-locked termination PROPERTIES LABELS "stress")

    add_executable(blocked_list_tests tests/blocked_list.cpp)
    target_link_libraries(blocked_list_tests PRIVATE parallel_tarjan)
    add_test(NAME blocked-list COMMAND blocked_list_tests)
    set_tests_properties(blocked-list PROPERTIES TIMEOUT 60)

    add_executable(spinlock_tests tests/spinlock_regression.cpp)
    target_link_libraries(spinlock_tests PRIVATE parallel_tarjan)
    add_test(NAME spinlock COMMAND spinlock_tests)
    set_tests_properties(spinlock PROPERTIES TIMEOUT 30)

    add_executable(open_addressed_tests tests/open_addressed_regression.cpp)
    target_link_libraries(open_addressed_tests PRIVATE parallel_tarjan)
    add_test(NAME open-addressed COMMAND open_addressed_tests)
    set_tests_properties(open-addressed PROPERTIES TIMEOUT 60)

    # This deterministic test specializes reference acquisition to reproduce
    # recycling between generation validation and pinning. Keep it in its own
    # executable to avoid mixing that specialization with production objects.
    add_executable(cell_reference_tests tests/cell_reference.cpp "Tarjan Code/typedefs.cpp")
    target_include_directories(cell_reference_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Tarjan Code")
    target_compile_features(cell_reference_tests PRIVATE cxx_std_17)
    target_link_libraries(cell_reference_tests PRIVATE Threads::Threads)
    if(PARALLEL_TARJAN_SANITIZER_FLAGS)
        target_compile_options(cell_reference_tests PRIVATE ${PARALLEL_TARJAN_SANITIZER_FLAGS})
        target_link_options(cell_reference_tests PRIVATE ${PARALLEL_TARJAN_SANITIZER_FLAGS})
    endif()
    add_test(NAME cell-reference COMMAND cell_reference_tests)
    set_tests_properties(cell-reference PROPERTIES TIMEOUT 30)

    # The vendored moodycamel queue uses fences that GCC's TSan does not model.
    # Test core SCC ownership with the synchronized queue explicitly, retaining
    # default-queue coverage in the normal and AddressSanitizer suites.
    set_property(TEST contention-locked blocked-list spinlock open-addressed cell-reference
        APPEND PROPERTY LABELS tsan-core)
endif()
