cmake_minimum_required(VERSION 3.16)
project(DrivOnixCpuTests LANGUAGES CXX)

enable_testing()
option(DRIVONIX_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)

add_library(cpu_under_test STATIC ../M68k/m68k.cpp ../Bits/bitsUtils.cpp cpuTestMemory.cpp)
target_compile_features(cpu_under_test PUBLIC cxx_std_11)
target_compile_definitions(cpu_under_test PUBLIC DRIVONIX_CPU_TESTS)

add_library(opcode_cases STATIC opcodeCoverage.cpp)
target_link_libraries(opcode_cases PUBLIC cpu_under_test)

add_executable(opcode_coverage opcodeCoverageMain.cpp)
target_link_libraries(opcode_coverage PRIVATE opcode_cases)

add_executable(legacy_cpu_tests unitTests.cpp)
target_link_libraries(legacy_cpu_tests PRIVATE opcode_cases)

add_executable(vector_worker conformance/vectorWorker.cpp)
target_link_libraries(vector_worker PRIVATE cpu_under_test)
find_package(Python3 3.9 COMPONENTS Interpreter REQUIRED)

if(MSVC)
    target_compile_options(cpu_under_test PRIVATE /utf-8 /bigobj)
    target_compile_options(opcode_cases PRIVATE /utf-8)
    target_compile_options(legacy_cpu_tests PRIVATE /utf-8)
elseif(DRIVONIX_SANITIZERS)
    foreach(target cpu_under_test opcode_cases opcode_coverage legacy_cpu_tests vector_worker)
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    endforeach()
    target_link_options(opcode_coverage PRIVATE -fsanitize=address,undefined)
    target_link_options(legacy_cpu_tests PRIVATE -fsanitize=address,undefined)
    target_link_options(vector_worker PRIVATE -fsanitize=address,undefined)
endif()

add_test(NAME opcode_reference COMMAND opcode_coverage)
add_test(NAME opcode_optimized COMMAND opcode_coverage --optimized)
add_test(NAME legacy_cpu_tests COMMAND legacy_cpu_tests)
set_tests_properties(opcode_reference opcode_optimized legacy_cpu_tests PROPERTIES TIMEOUT 30)

add_test(NAME conformance_adapter COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/conformance/test_adapter.py --worker $<TARGET_FILE:vector_worker>)
set_tests_properties(conformance_adapter PROPERTIES TIMEOUT 60)

add_test(NAME handwritten_known_failure_gate COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/conformance/run.py --worker $<TARGET_FILE:vector_worker>
    --handwritten-only --baseline ${CMAKE_CURRENT_SOURCE_DIR}/conformance/handwritten-baseline.json
    --report-dir ${CMAKE_CURRENT_BINARY_DIR}/handwritten-report)
set_tests_properties(handwritten_known_failure_gate PROPERTIES TIMEOUT 60)
