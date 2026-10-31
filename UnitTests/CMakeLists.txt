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

# Optional independent reference: downloaded sources must already match the lock.
set(DRIVONIX_REFERENCE_DIR "" CACHE PATH "Verified Musashi source directory")
if(DRIVONIX_REFERENCE_DIR)
    enable_language(C)
    get_filename_component(reference_cache "${DRIVONIX_REFERENCE_DIR}/.." ABSOLUTE)
    execute_process(COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/conformance/prepare_reference.py
        --cache ${reference_cache} --verify-only RESULT_VARIABLE reference_verified)
    if(NOT reference_verified EQUAL 0)
        message(FATAL_ERROR "Independent reference sources failed verification")
    endif()
    add_executable(reference_make ${DRIVONIX_REFERENCE_DIR}/m68kmake.c)
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/m68kops.c ${CMAKE_CURRENT_BINARY_DIR}/m68kops.h
        COMMAND reference_make ${CMAKE_CURRENT_BINARY_DIR} ${DRIVONIX_REFERENCE_DIR}/m68k_in.c
        DEPENDS reference_make ${DRIVONIX_REFERENCE_DIR}/m68k_in.c VERBATIM)
    add_library(independent_68000 STATIC ${DRIVONIX_REFERENCE_DIR}/m68kcpu.c
        ${CMAKE_CURRENT_BINARY_DIR}/m68kops.c ${DRIVONIX_REFERENCE_DIR}/m68kdasm.c
        ${DRIVONIX_REFERENCE_DIR}/softfloat/softfloat.c conformance/referenceBridge.c)
    target_include_directories(independent_68000 PUBLIC ${DRIVONIX_REFERENCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})
    target_compile_definitions(independent_68000 PUBLIC M68K_EMULATE_010=0 M68K_EMULATE_EC020=0
        M68K_EMULATE_020=0 M68K_EMULATE_030=0 M68K_EMULATE_040=0 M68K_EMULATE_PMMU=0
        M68K_EMULATE_ADDRESS_ERROR=1 M68K_INSTRUCTION_HOOK=1 M68K_EMULATE_TRACE=1 M68K_EMULATE_RESET=1 M68K_EMULATE_PREFETCH=1)
    add_executable(reference_worker conformance/vectorWorker.cpp conformance/referenceAdapter.cpp cpuTestMemory.cpp)
    target_compile_definitions(reference_worker PRIVATE DRIVONIX_CPU_TESTS DRIVONIX_REFERENCE)
    target_compile_features(reference_worker PRIVATE cxx_std_11)
    target_link_libraries(reference_worker PRIVATE independent_68000)
    if(MSVC)
        target_compile_options(independent_68000 PRIVATE /bigobj /utf-8)
        target_compile_options(reference_worker PRIVATE /utf-8)
    else()
        target_link_libraries(independent_68000 PUBLIC m)
    endif()
endif()

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

add_test(NAME handwritten_conformance COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/conformance/run.py --worker $<TARGET_FILE:vector_worker>
    --handwritten-only
    --report-dir ${CMAKE_CURRENT_BINARY_DIR}/handwritten-report)
set_tests_properties(handwritten_conformance PROPERTIES TIMEOUT 60)

add_test(NAME complete_suite_adapter COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/conformance/test_complete.py --worker $<TARGET_FILE:vector_worker>)
set_tests_properties(complete_suite_adapter PROPERTIES TIMEOUT 60)

# Both normal and sanitizer runs require correct state and timing; historical
# known-failure snapshots are retained only as audit history, not pass criteria.
set(reference_validation_args "")
if(TARGET reference_worker)
    set(reference_validation_args --oracle $<TARGET_FILE:reference_worker>)
endif()
set(specification_profile normal)
if(DRIVONIX_SANITIZERS)
    set(specification_profile sanitizers)
endif()
add_test(NAME complete_instruction_specifications COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/conformance/complete.py --worker $<TARGET_FILE:vector_worker>
    ${reference_validation_args} --specifications-only --profile ${specification_profile}
    --report-dir ${CMAKE_CURRENT_BINARY_DIR}/specifications-report)
set_tests_properties(complete_instruction_specifications PROPERTIES TIMEOUT 300)
