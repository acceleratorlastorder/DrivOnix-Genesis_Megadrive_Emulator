Z80 and sound implementation
============================

Production Z80 state and instruction execution use the cycle-stepped zlib-
licensed floooh/chips core through core.c. That C wrapper is shared by the
console's memory/bus integration and vectorworker.cpp. The upstream file is
unmodified; the wrapper adds the NMOS Q/P/EI latches and the documented hardware
quirks for SCF/CCF, DD/FD before SCF/CCF, and repeating block instructions.

Hardware-derived undocumented flag reference:
https://github.com/hoglet67/Z80Decoder/wiki/Undocumented-Flags

The complete existing pinned Z80 corpus was executed strictly on 2026-10-31:
1,604 instruction/prefix groups, 1,604,000 cases, 1,604,000 passes, zero failures.
Comparison includes all requested architectural/undocumented registers, RAM,
I/O transactions and total instruction T-states. The worker never receives
expected final states. No per-pin trace is emitted, so passing this run does not
certify pin-level bus waveforms or every possible interrupt/fault sequence.

Reproduce after building the z80_worker target:

  python unittests/conformance/z80.py --fetch --worker build/linux/z80_worker

The full pinned dataset is about 1.3 GB. A faster sample can select groups:

  python unittests/conformance/z80.py --fetch --worker build/linux/z80_worker --group 00 --group 37 --group "dd 37" --group "dd cb __ 46" --group "ed b2"

On Windows a multi-configuration generator puts z80_worker.exe under the
selected configuration directory, for example build/windows/Release/.

Console integration
-------------------

Z80::Init installs callbacks for the 24-bit 68k banked window, resets the CPU
and initializes its 8 KiB RAM. SetReset later resets the CPU but preserves RAM.
SetBusRequest requests the 68k's ownership at an instruction boundary; direct
68k Z80-window accesses are accepted only after BusGranted is asserted.
The 9-bit bank register, mirrored RAM, YM2612 ports and PSG writes are mapped.
The fixed $7F00-$7F0F VDP alias uses the same callbacks as the 68k banked
window. It selects the 68k high/low byte lane on reads and duplicates a byte
onto both word lanes on data/control writes; it is not the SMS control-port
protocol. Upper unused Z80 status bits read high. Odd PSG addresses $7F11,
$7F13, $7F15 and $7F17 select the same sound register. Invalid addresses return
$FF or ignore writes; physical bus lockups and the VDP test port are not modeled.
Address/lane references: Sega Genesis Software Manual, section 6 (Z80 control),
and the hardware bus behavior documented by the Genesis Plus GX authors:
https://raw.githubusercontent.com/ekeeke/Genesis-Plus-GX/master/core/memz80.c
https://raw.githubusercontent.com/ekeeke/Genesis-Plus-GX/master/core/membnk.c
No implementation or lookup tables from that project are included.
VDP FIFO waits can advance console time from inside a Z80 memory callback.
During that nested advance, audio and the CPU clock phase advance while the
instruction engine remains blocked until its pending data access completes.
This prevents recursive instruction execution; the WAIT pin waveform and shared
68k/Z80 arbitration delay are still approximations rather than pin-level timing.
Interrupt drives the Z80 INT input; the console should use the VDP's Z80 IRQ.
YM2612's IRQ output is not wired automatically to the Z80.

Z80::Advance receives console master clocks and advances BOTH sound and CPU,
including while CPU reset or bus ownership stops execution. Do not separately
advance Audio for the same elapsed time. Z80 register writes are timestamped
at their own master-clock edge, not at the end of the host's scheduling slice.

Audio::Init selects the console master clock and output rate (default 48 kHz).
The YM2612 clocks at master/7 using Aaron Giles's BSD-licensed ymfm engine;
timers and busy flags are scheduled in those input clocks. The original Sega
PSG implementation clocks at master/15 with a divide-by-16 tone stage and
16-bit noise shift register. Stereo mixing integrates signal levels over each
output interval and removes the analog DC component. DrainSamples returns
interleaved signed 16-bit stereo frames and consumes the queue. An undrained
queue is bounded to two seconds. The implementation does not access a host
sound device; the frontend owns playback and underrun handling.

hardwaretests.cpp checks CPU execution, prefixes, RAM mirrors, banking, bus
ownership, reset, HALT/IM1/RETI, audio clock/sample counts in PAL/NTSC, PSG tones
and mute, FM timers/busy/status, DAC/panning and host-slice-independent output.
Both ordinary and ASan/UBSan runs pass. Dependency sources and license notices
are pinned in thirdparty/sound-z80.lock.json; run thirdparty/verify_sources.py
to check them offline.
unittests/frontend/z80bustests.cpp executes real Z80 memory instructions against
the console VDP, including queued-FIFO read waits, continuation after a wait,
all data/control/HV mirrors, PSG aliases and exact audio clock accounting with
different host scheduling slices.
