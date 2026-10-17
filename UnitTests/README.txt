CPU opcode tests
================

Requires CMake, a C++11 compiler, and Python 3.9 or newer. Build and run without
Windows, CSFML, a display, or a game ROM:

    cmake -S UnitTests -B build/cpu -DCMAKE_BUILD_TYPE=Debug
    cmake --build build/cpu --config Debug --parallel 2
    ctest --test-dir build/cpu -C Debug --output-on-failure

The CMake target compiles the actual M68k/m68k.cpp and Bits/bitsUtils.cpp.
DRIVONIX_CPU_TESTS selects cpuTestMemory.cpp as an isolated, big-endian memory
fixture instead of the console's display/I/O dependencies. It preserves
the current RAM mapping and read-only ROM writes. This is CPU unit coverage;
it does not validate Genesis I/O, rendering, audio, ROM booting, or game behavior.

opcode_reference and opcode_optimized run the same ten asserted test groups.
The second uses M68k::Update(), including its opcode fetch and generated switch
dispatch, which is the path the emulator uses. Tests check register results,
defined flags, reserved status bits, privilege rejection, and operand PC advance.
MOVE USP is checked by transferring two different values through USP into other
address registers. DIVU includes remainder packing, zero quotient, unsigned
quotients 0x8000/0xFFFF, and overflow. MULU includes zero, a nonzero product with
a zero low word, and products setting bit 31. Undefined DIVU overflow N/Z flags
are deliberately not asserted. Divide-by-zero exception delivery and full
exception stack frames are not covered by these groups.

legacy_cpu_tests retains 17 asserted C++ groups. The 67 empty templates have been
replaced by real specifications in conformance/specifications.py. All 84 groups
from the original registry are required by conformance/legacy-groups.json and
checked by the expanded specification suite, not counted as placeholder passes.
All legacy debug states are zero initialized, and a failed test returns a
nonzero process exit code. The runner no longer waits for an interactive pause.
The ADDA checks use address registers and verify flag preservation; the old
test wrote beyond the data-register array. ASL/ASR memory and zero-count
register cases now have assertions, including operand and X-flag preservation.

conformance_adapter checks the external-vector decoder, protocol, comparison
logic, failure recovery, and regression gate using independent fixtures and
deliberately corrupted results. handwritten_known_failure_gate runs 28 readable
specifications through both dispatch paths. Seven currently fail per path; this
gate permits only the recorded wrong bits while keeping the correct expected
outputs. A passing regression gate does not mean those CPU cases pass.

complete_suite_adapter checks the expanded specifications and Z80 worker contract.
complete_instruction_specifications runs 8,089 instruction cases plus 40 sequence
cases through both dispatch paths, using an explicit known-failure regression
baseline. An optional pinned independent reference checks the specifications and
every one of the 65,536 opcode words, including illegal encodings. See
conformance/COMPLETE.txt for the full sweep, eight operand patterns, and Z80 data.

The pinned SingleStepTests/m68000 corpus and original instruction matrix are
documented in conformance/README.txt. Fetching and running external data is explicit;
ordinary CTest needs no network. CI also runs a fixed external sample and saves
the diagnostic reports as artifacts. DocUnitTestM68K.pdf remains the original
test-design guide; use its source/destination/flags/addressing approach when
adding focused cases alongside the external vectors.

For address/undefined-behavior sanitizer checks with GCC or Clang:

    cmake -S UnitTests -B build/cpu-sanitized -DCMAKE_BUILD_TYPE=Debug -DDRIVONIX_SANITIZERS=ON
    cmake --build build/cpu-sanitized --parallel 2
    ctest --test-dir build/cpu-sanitized --output-on-failure

The original UnitTests/compile.bat still builds the Windows suite with the real
Genesis implementation and the existing MinGW/CSFML prerequisites.

Both ordinary and sanitizer CTest run seven targets. The expanded CPU suite uses
a separate reviewed sanitizer profile so existing unsafe cases remain reported
as crashes and any newly unsafe case fails CI. Those known crashes are not
sanitizer-clean CPU passes. See COMPLETE.txt for strict and diagnostic commands.

Hardware expectations follow the Motorola M68000 Family Programmer's Reference
Manual (MC68000 forms, not the later 68010/68020 variants):
https://www.nxp.com/docs/en/reference-manual/M68000PRM.pdf

Relevant sections: status register 1.3.2, DIVU 4-96, MULU 4-138,
MOVE from SR 4-125, and MOVE USP 6-21.
