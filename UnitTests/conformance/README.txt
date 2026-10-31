MC68000 independent executable specifications
============================================

The original DrivOnix MC68000 implementation is repaired in place. Tests combine
small readable cases explaining an instruction's behavior with independently
supplied single-step vectors. The original DocUnitTestM68K.pdf method remains
useful: source, destination, flags, addressing, timing and observable sequences.
No replacement 68000 core is linked into production.

COMPLETE.txt documents all 84 original specification groups, the all-opcode-word
sweep, sanitizer checks and the production Z80 worker. This file documents the
28 handwritten JSON examples and the separate pinned MAME corpus.

Offline checks and independent data
----------------------------------

    cmake -S UnitTests -B build/cpu -DCMAKE_BUILD_TYPE=Debug
    cmake --build build/cpu --config Debug --parallel 2
    ctest --test-dir build/cpu -C Debug --output-on-failure
    python UnitTests/conformance/run.py --worker build/cpu/vector_worker --handwritten-only

Fetch immutable external data and strictly execute every active group:

    python UnitTests/conformance/fetch.py --cache build/conformance-data
    python UnitTests/conformance/run.py --worker build/cpu/vector_worker --active-only --report-dir build/conformance-report

The active run executes 125 reliable groups of 2,500 cases per decoder plus the 28
handwritten cases: 625,056 total evaluations. On 2026-10-31 all 625,056 passed with
zero failures, crashes or decoder divergences. 50 handwritten evaluations lack
cycle expectations; external vectors assert supplied instruction cycle totals,
including operand-dependent multiply/divide timing.

Upstream explicitly warns that TAS and TRAPV external data is unreliable. Their
5,000 cases per decoder remain locked and inventoried, with exclusion counts and
reasons in the --active-only report. They are never counted as passes. Omitting
--active-only reports them as quarantined and deliberately returns a nonzero
strict status even when all active instructions pass. An explicit quarantined
--group cannot be combined with --active-only. Readable specifications and the
complete-word sweep independently execute TAS and both TRAPV outcomes.

Useful smaller strict runs:

    python UnitTests/conformance/run.py --worker build/cpu/vector_worker --group DIVS
    python UnitTests/conformance/run.py --worker build/cpu/vector_worker --active-only --limit-per-group 8 --report-dir build/conformance-sample

Windows multi-configuration Debug builds use build/cpu/Debug/vector_worker.exe.
--dispatch reference or --dispatch optimized selects a decoder; default both
includes the production Update path. The label reference here denotes the old
DrivOnix decoder, not the optional independent reference_worker.
--limit-per-group 0 means all vectors. Positive limits choose a deterministic
spread independently of CPU outcomes; handwritten cases always run.

Strict is the default: wrong state/timing, crashes, unsupported inputs or an
unexcluded quarantine produce exit 1. Missing/corrupt data and adapter errors
produce exit 2. --mode audit emits the same failing results but exits 0 when the
audit completes; that exit code is not an instruction pass. Reports contain
report.json and instructions.csv with exact source/expectation hashes, selected
counts, state/timing categories, expected/actual differences and decoder checks.

Sources and scope
-----------------

Execution vectors: https://github.com/SingleStepTests/m68000
Commit: 64b253116a3de04aaac4346c43680960dc9b67e5
MIT license: upstream-license.txt. corpus.py implements the documented binary
format without executing downloaded source. 127 files contain 2,500 cases each;
125 are active and 2 unreliable groups are excluded only when requested explicitly.

Encoding catalogue: https://github.com/SingleStepTests/680x0
Commit: e0d5ece9670205cc84a0101081837deb446f86a3
Only map/68000.official.json is used. It is a coverage aid, not an exhaustive
hardware specification: it omits valid ILLEGAL 0x4AFC. The matrix separates
catalogued, selected and executed encodings. No observed failure in an encoding
proves correctness for every operand value or bus state.

upstream.lock.json pins each file's byte count and Git blob SHA. Both fetch and
run verify the data and reject a corrupt existing cache. External vectors are
not vendored. Readable expectations follow the MC68000 forms in Motorola's
programmer manual, https://www.nxp.com/docs/en/reference-manual/M68000PRM.pdf
handwritten.json supplies initial registers/RAM, instruction words and expected
state. Unspecified registers and seeded memory must remain unchanged. 25 of 28
handwritten cases intentionally do not supply a cycle expectation.

The worker compiles the production CPU with a test-only loader and a writable
24-bit big-endian bus. Legacy checks retain their Genesis-style fixture. It
receives initial state and addresses to observe, never expected results. The
adapter subtracts the MAME prefetch-PC bias, converts word RAM to bytes and checks
D0-D7, A0-A7, USP/SSP, architectural PC, defined SR bits, changed RAM and available
cycles. Conflicting RAM/prefetch is unsupported and never silently passes.

MAME vectors describe one instruction before subsequent boundary trace service.
The worker's explicit instruction profile preserves SR.T while stepping those
semantics. Production Update and readable sequence cases still execute real
trace exceptions. STOP's architectural PC is normalized for the corpus's stopped
prefetch position. These execution profiles are included in expectation hashes.

Hardware-undefined BCD N/V, divide-overflow N/Z and divide-zero N/Z/V are
masked. A successful CHK leaves NZVC undefined; a taken bounds trap defines N,
so both live and saved N remain checked while only C/V/Z are masked. Independent
readable guards and the complete-word sweep also assert trap N. The adapter
corruption tests prove that wrong live/saved N is detected. The same undefined
divide bits in saved SR are masked. Address-error SSW bits 15..5
are undefined, while direction, instruction/data and function-code bits 4..0
remain asserted. IR, fault address, saved PC and other defined frame bytes remain
checked by the independent vectors. ORACLE-CORRECTIONS.txt separately documents
bounded corrections to the optional differential reference; MAME expected results
are not rewritten to fit DrivOnix.

Acceptance and remaining boundaries
----------------------------------

CTest and CI use strict commands without known-failure baseline arguments. Old
baseline JSON/gzip files and BASELINE.txt are historical evidence of earlier
failures, not current acceptance criteria. They are not automatically regenerated.
A new readable case should assert independently justified hardware behavior,
fail before the corresponding fix, and pass through both decoders afterward.
Run its external instruction group and applicable sanitizers as well.

CI runs seven strict CPU CTest targets on Linux and Windows, an active external
sample, every opcode word with one input pattern and Linux ASan/UBSan checks.
Generated reports are uploaded as artifacts. Full active MAME and eight-pattern
sweeps remain available using these commands and COMPLETE.txt.

These CPU fixtures do not validate electrical bus pins, byte strobes, wait states,
arbitration, the final internal prefetch queue or every fault/interrupt sequence.
The console's VDP, FM/PSG audio, Z80, memory mapping, saves and controllers have
separate implementation tests. CPU passes do not establish a commercial-game
compatibility percentage. COVERAGE.txt records the measured layers and limits.
