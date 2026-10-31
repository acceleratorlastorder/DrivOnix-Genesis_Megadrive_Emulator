Mode-5 video implementation
===========================

The scanline renderer resolves plane A, plane B, the window replacing A, and
sprites with their hardware priority classes. Sprite ordering is resolved once
in SAT order before compositing with planes: an earlier low-priority sprite
still masks a later high-priority sprite where it is opaque. Color index zero
is transparent; RGB values such as magenta are ordinary opaque colors.

Pattern fetches use 8x8 tiles normally and 8x16 tiles in interlace mode 2,
including horizontal/vertical flipping, sprite column-major tile ordering,
plane scrolling, two-cell vertical scrolling, the H40 window base mask, and
window horizontal/vertical boundaries. Sprite collision and per-line limits
are tracked. Shadow/highlight uses the VDP's four-bit brightness ladder and
palette-3 sprite operator colors.

Interlace mode 2 weaves the two fields into a 448-line or PAL 480-line RGBA
framebuffer. The framebuffer stride remains 320 pixels. The display scales
interlaced output vertically by one half, with linear filtering to combine
adjacent field lines, while screenshots retain the complete field resolution.
Normal 224/240-line modes retain nearest-neighbor scaling. Headless mode and
the frontend share the exact same renderer/framebuffer.

H32 and H40 horizontal counters are computed relative to the HINT/V-counter
increment phase, including discontinuities and the H40 variable sync clock.
HBLANK status edges and delayed VINT onset use master-clock positions. A Z80
interrupt pulse spans one scanline. Status reads clear sprite collision and
overflow, while CPU interrupt acknowledgement clears the pending IRQ source;
acknowledging vertical IRQ does not discard a pending horizontal IRQ. The
register-controlled HV counter freeze preserves both counters until released.

Protocol and hardware-timing references:
https://nemesis.hacking-cult.org/MegaDrive/Documentation/GenesisSoftwareManual.pdf
https://github.com/Emu-Docs/Emu-Docs/tree/master/Genesis
https://github.com/ekeeke/Genesis-Plus-GX/blob/master/core/hvc.h
The implementation is original DrivOnix code; no renderer or lookup-table
source from Genesis Plus GX is incorporated. Numeric counter/clock edges and
board layouts are treated as hardware facts.

Validation
----------

UnitTests/frontend/vdpTests.cpp independently checks fixed-color tile/palette
scenes, plane/sprite priority combinations, hidden-sprite ordering, collision
status clearing, window transparency/replacement, byte fetches and flips,
window table alignment, two-cell scroll columns, multi-tile sprite layout,
shadow/highlight operators, opaque magenta, woven interlace field patterns,
H32/H40 counter edges, HBLANK edges, HV freeze, and IRQ latch priority.
Normal and ASan/UBSan builds pass these checks as well as consoleTests.cpp's
existing FIFO, DMA, memory, controller and cartridge checks.

Accuracy boundaries
-------------------

Rendering is scanline-based: arbitrary mid-line palette/register/VRAM writes
are not yet resolved pixel by pixel through the VDP's internal fetch pipeline.
The internal sprite-attribute cache and every undocumented sprite-mask/fetch
quirk are not modeled in full. Interlaced image fields are implemented, but
half-line sync differences between fields and every overscan/border mode are
not certified. These checks establish the stated scenes and clock edges,
not universal game compatibility or a complete pin-level VDP implementation.
