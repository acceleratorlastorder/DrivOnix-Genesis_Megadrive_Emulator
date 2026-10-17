MC68000 executable specifications
================================

The expanded suite is documented in COMPLETE.txt: all 84 original instruction
groups now have real specifications, all 65,536 opcode words are exercised, and
the Z80 corpus/worker contract is prepared. This file describes the first
handwritten/MAME corpus layer, which remains independently available.

Keep two complementary kinds of tests: small, readable project cases explaining
an instruction's result, and broad independent single-step vectors. This work
adds the baseline for correcting the CPU later; it changes no opcode handlers.
The original DocUnitTestM68K.pdf describes the source, destination, flags, and
addressing-mode method used by the project tests.

Build and run the offline checks from the repository root:

    cmake -S UnitTests -B build/cpu -DCMAKE_BUILD_TYPE=Debug
    cmake --build build/cpu --config Debug --parallel 2
    ctest --test-dir build/cpu -C Debug --output-on-failure

On Windows replace build/cpu/vector_worker below with
build/cpu/Debug/vector_worker.exe for a multi-configuration Debug build.

Fetch the immutable external data, then run every case:

    python UnitTests/conformance/fetch.py --cache build/conformance-data
    python UnitTests/conformance/run.py --worker build/cpu/vector_worker

Strict mode is the default: mismatches, CPU crashes, unsupported cases, and
quarantined vectors produce exit 1. Missing/corrupt data or adapter errors produce
exit 2. Current CPU failures are expected. For a diagnostic run that exits 0 after
successfully producing reports, add --mode audit. Audit success certifies only
that the audit completed. Reports include totals, state-versus-timing failures,
correct expected and actual values for examples, dispatch disagreements, source
pins, and an instruction/encoding matrix in report.json and instructions.csv.

Useful smaller runs:

    python UnitTests/conformance/run.py --worker build/cpu/vector_worker --handwritten-only
    python UnitTests/conformance/run.py --worker build/cpu/vector_worker --group DIVS --mode audit
    python UnitTests/conformance/run.py --worker build/cpu/vector_worker --limit-per-group 8 --baseline UnitTests/conformance/smoke-baseline.json

--dispatch reference or --dispatch optimized selects one decoder; the default
executes both, including the optimized Update path used by the emulator.
--limit-per-group 0 means every vector. Positive limits select a fixed spread
through each group independently of CPU outcomes. Handwritten cases always run.
--report-dir chooses the output directory; generated reports/data stay in build/.

Sources and scope
-----------------

Execution vectors: https://github.com/SingleStepTests/m68000
Commit: 64b253116a3de04aaac4346c43680960dc9b67e5
MIT license reproduced in upstream-license.txt. corpus.py implements the binary
format documented by that project's decode.py; it does not execute fetched code.
The 127 files contain 2,500 cases each. Upstream explicitly warns that TAS and
TRAPV vectors have problems, so their 5,000 cases are quarantined with reasons,
not counted as passes. Separate readable TAS and TRAPV cases still execute.

Encoding catalogue: https://github.com/SingleStepTests/680x0
Commit: e0d5ece9670205cc84a0101081837deb446f86a3
Only map/68000.official.json is downloaded from that source. The map is a coverage
aid, not a definitive hardware specification: for example it omits 0x4AFC even
though ILLEGAL is an MC68000 instruction. The matrix separates catalogued,
selected, and executed encodings; an encoding with no observed failures is not
proof that all operand values or behaviors work. Handwritten family labels may
be broader than the catalogue's size-specific labels. No external vector files
are vendored. upstream.lock.json pins each download's bytes and Git blob SHA;
both fetch and run verify them. Existing corrupt cache entries fail closed.

Readable expectations follow the MC68000 forms in Motorola's programmer manual:
https://www.nxp.com/docs/en/reference-manual/M68000PRM.pdf
handwritten.json stores instruction words, initial registers/RAM, and expected
registers/flags/PC/RAM. Unspecified registers and seeded RAM must stay unchanged.
Cycles are asserted only when explicitly supplied; 25 of the 28 readable cases
currently leave timing unchecked. External vectors supply cycle totals.

The worker compiles the real CPU with a test-only state loader and a writable
24-bit flat CPU bus. Ordinary legacy tests retain their existing Genesis-style
fixture. This isolates instruction behavior from console devices and ROM write
restrictions; it does not validate the Genesis memory map, VDP, sound, Z80,
interrupt scheduling, game booting, or full emulator compatibility.

The adapter subtracts four from initial and final MAME prefetch PCs to get the
architectural PC, converts word RAM records into big-endian bytes, and seeds the
initial instruction prefetch. Conflicting prefetch/RAM is unsupported, never a
pass. It checks D0-D7, A0-A7, USP/SSP, PC, defined SR bits, final RAM (including
unexpected changed writes), and available cycle totals. Stop state is checked
when specified by a readable case. It does not observe the final prefetch queue,
bus ordering/function codes/byte strobes, or external RESET effects.

Undefined BCD N/V, divide-overflow N/Z, and divide-by-zero N/Z/V are masked.
CHK's NZVC are not asserted by this adapter, including the path-dependent N.
Other defined and reserved SR bits remain checked. These limitations are explicit
in the report; no result claims complete hardware conformance.

Regression gates and future fixes
---------------------------------

The checked-in handwritten baseline and deterministic eight-per-group external
baseline record existing failures by case, decoder, field, and individual wrong
bits. Correct expected outputs remain in the specifications. Fixing a wrong bit
is allowed; introducing another wrong bit, a failure in a previously passing
field/case, a new crash, or changed vectors/selection fails the gate. Quarantine
and unsupported status remain visible even when an existing status is allowed.
The gate compares the selected expectations and source pins, using canonical
JSON hashes so checkout line endings do not change the plan.

CI runs seven ordinary CTest targets, the fixed external sample and complete
opcode-word regression sweep on Ubuntu and Windows, and seven targets with
address/undefined-behavior sanitizers on Linux. Expanded unsafe CPU cases remain
reported as crashes under a separate reviewed regression profile that rejects
new failures (see COMPLETE.txt). CI uploads diagnostic
diagnostic reports. Green CI means these regression checks passed, not that all
MC68000 vectors pass. Full-corpus runs remain available with the commands above.

To implement an opcode, add readable edge cases following the original guide,
run the relevant external group in strict/audit mode, fix the CPU, then check both
dispatch paths and the regression gates. Keep correct hardware expectations.
Changed specifications or corpus selections require deliberate baseline review;
do not automatically regenerate baselines as part of CI. The explicit
--record-baseline PATH option exists for that review and is separate from
--baseline PATH. A new readable specification changes the plan, so run strict or
audit first, inspect all differences, and review the regenerated snapshot.

See BASELINE.txt for the first full diagnostic run and COVERAGE.txt for the
expanded suite. Both documents preserve failures rather than changing expected
hardware results to match DrivOnix.
