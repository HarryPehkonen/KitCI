cmake_minimum_required(VERSION 3.16)
project(kit-ci LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(KITCI_BUILD_TESTS "build the unit test suite" ON)
option(KITCI_BUILD_FUZZ "build the libFuzzer target (needs clang)" OFF)

if(KITCI_BUILD_FUZZ)
    # The whole fuzz build is instrumented. libFuzzer's own entry point is added per-target
    # below, not to the global flags: kit-ci defines its own main() and would collide with it.
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address,undefined -fno-omit-frame-pointer -g")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address,undefined")
endif()

add_library(kitci STATIC src/parser.cpp src/runner.cpp src/graph.cpp)
target_include_directories(kitci PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

add_executable(kit-ci src/main.cpp)
target_link_libraries(kit-ci PRIVATE kitci)

if(KITCI_BUILD_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)
    add_executable(kitci_tests tests/parser_test.cpp tests/runner_test.cpp
        tests/graph_test.cpp tests/cli_test.cpp)
    target_link_libraries(kitci_tests PRIVATE kitci GTest::gtest GTest::gtest_main)
    target_compile_definitions(kitci_tests PRIVATE
        KITCI_BINARY="$<TARGET_FILE:kit-ci>"
        KITCI_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
    )
    add_dependencies(kitci_tests kit-ci)
    include(GoogleTest)
    gtest_discover_tests(kitci_tests)
endif()

if(KITCI_BUILD_FUZZ)
    add_executable(fuzz_gate_toml fuzz/fuzz_gate_toml.cpp)
    target_link_libraries(fuzz_gate_toml PRIVATE kitci)
    target_compile_options(fuzz_gate_toml PRIVATE -fsanitize=fuzzer)
    target_link_options(fuzz_gate_toml PRIVATE -fsanitize=fuzzer)
else()
    # libFuzzer only links under clang, but the fuzz translation unit is still source this
    # repo lints and formats. Compiling it into an object library keeps it in
    # compile_commands.json — a file the compile database does not cover is a file the lint
    # stage silently skips.
    add_library(fuzz_source OBJECT fuzz/fuzz_gate_toml.cpp)
    target_link_libraries(fuzz_source PRIVATE kitci)
endif()

# Warnings are errors (SPEC.md §6). gcc and clang do not agree about warnings, which is the
# reason the gate builds with both — so both get the same flags and both must be silent.
foreach(target kitci kit-ci kitci_tests fuzz_source fuzz_gate_toml)
    if(TARGET ${target})
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Werror -Wshadow
        )
    endif()
endforeach()
