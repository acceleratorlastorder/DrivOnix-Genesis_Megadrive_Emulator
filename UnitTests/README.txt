CPU opcode tests
================

The original MC68000 core is repaired in place. These tests compile the same
M68k/m68k.cpp and Bits/bitsUtils.cpp used by the emulator, including the production
fetch, legality checks and generated decoder. No replacement 68000 core is linked
into the emulator. The independent oracle is an optional test target only.

Build and run the seven offline CTest targets with CMake, a C++11 compiler and
Python 3.9 or newer; no display, CSFML, game ROM or network is needed:

    cmake -S UnitTests -B build/cpu -DCMAKE_BUILD_TYPE=Debug
    cmake --build build/cpu --config Debug --parallel 2
    ctest --test-dir build/cpu -C Debug --output-on-failure

opcode_reference and opcode_optimized check the existing asserted C++ cases with
both decoders. legacy_cpu_tests retains the 17 original asserted groups.
conformance_adapter and complete_suite_adapter validate the input decoder,
comparison masks, worker protocol and crash recovery with independent fixtures
and deliberately corrupted results. handwritten_conformance strictly executes
all 28 JSON cases on both decoders. complete_instruction_specifications strictly
executes 8,155 readable instruction cases and 42 multi-instruction sequences per
decoder, covering all 84 original groups. The 67 old empty C++ templates are not
executed or counted as passes.

A pass requires the expected state and every supplied cycle count. Historical
known-failure snapshots remain as audit history; neither CTest nor CI accepts
known wrong results through a baseline. Strict commands return nonzero on wrong
state, timing or crashes. --mode audit is available to finish a diagnostic report;
its zero exit status is not a CPU conformance pass.

DRIVONIX_CPU_TESTS selects the isolated big-endian test bus. External vectors use
a writable 24-bit flat bus; legacy checks retain their Genesis-style fixture.
This separates instruction semantics from console devices. It does not establish
correct VDP rendering, sound, bus arbitration or compatibility with game ROMs.
The root CMake project has separate console, VDP, audio, Z80 and frontend tests.

The expanded cases follow the original DocUnitTestM68K.pdf method: source and
destination values, flags, addressing, timing and observable sequences. They
include full shift counts, arithmetic boundaries, BCD, all condition codes,
status/stack banks, synchronous exceptions, interrupts, STOP/reset, address-error
frames and self-modifying code that observes instruction prefetch. Undefined
hardware bits are masked narrowly; correct expected outputs are never captured
from DrivOnix. See conformance/COMPLETE.txt and conformance/ORACLE-CORRECTIONS.txt.

For GCC/Clang address and undefined-behavior sanitizer checks:

    cmake -S UnitTests -B build/cpu-sanitized -DCMAKE_BUILD_TYPE=Debug -DDRIVONIX_SANITIZERS=ON
    cmake --build build/cpu-sanitized --parallel 2
    ctest --test-dir build/cpu-sanitized --output-on-failure

These are the same seven strict targets and the same hardware expectations.
Sanitizer failures are CPU failures; no known-crash allowance is supplied.

conformance/README.txt documents the pinned independent MAME vectors.
conformance/COMPLETE.txt gives the optional independent all-opcode sweep and the
production Z80 worker commands. conformance/COVERAGE.txt records measured scope.
CI runs strict CPU checks on Linux and Windows and sanitizer checks on Linux,
then uploads readable reports. Ordinary CTest never downloads external data.

The original compile.bat remains a legacy Windows build using MinGW/CSFML.
Hardware references select MC68000 forms, not later 68010/68020 instructions:
https://www.nxp.com/docs/en/reference-manual/M68000PRM.pdf
