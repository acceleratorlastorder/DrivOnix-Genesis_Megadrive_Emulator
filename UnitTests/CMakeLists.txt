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

if(MSVC)
    target_compile_options(cpu_under_test PRIVATE /utf-8 /bigobj)
    target_compile_options(opcode_cases PRIVATE /utf-8)
    target_compile_options(legacy_cpu_tests PRIVATE /utf-8)
elseif(DRIVONIX_SANITIZERS)
    foreach(target cpu_under_test opcode_cases opcode_coverage legacy_cpu_tests)
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    endforeach()
    target_link_options(opcode_coverage PRIVATE -fsanitize=address,undefined)
    target_link_options(legacy_cpu_tests PRIVATE -fsanitize=address,undefined)
endif()

add_test(NAME opcode_reference COMMAND opcode_coverage)
add_test(NAME opcode_optimized COMMAND opcode_coverage --optimized)
add_test(NAME legacy_cpu_tests COMMAND legacy_cpu_tests)
set_tests_properties(opcode_reference opcode_optimized legacy_cpu_tests PROPERTIES TIMEOUT 30)
