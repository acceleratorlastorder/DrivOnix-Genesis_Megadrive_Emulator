Cartridge region selection
==========================

The 16-byte region header at $1F0 is read without changing the cartridge image.
RomLoader::GetCountry returns its original first byte. GetHardwareRegion returns
the canonical selected 'J', 'U' or 'E' for the emulator's console hardware.

Supported ASCII forms, case-insensitively:
  - J/U/E letters, alone or combined, optionally separated/padded with spaces;
  - the complete names JAPAN, USA and EUROPE;
  - a hexadecimal region mask, optionally prefixed with 0x or padded with zeros.

Mask bit0 selects Japan NTSC, bit2 USA NTSC and bit3 Europe PAL. Selection prefers
USA, then Japan, then Europe. A standalone E is the canonical Europe letter;
0xE is the hexadecimal mask14, which includes USA. F is the hexadecimal mask15.
Bit1-only domestic PAL hardware is not selected by this base-console build.
Empty, unknown, non-ASCII/control-byte and unsupported fields default to USA.
NUL bytes are accepted as header padding. No filename/database overrides occur.

Genesis::SetHardwareRegion/GetHardwareRegion retain the selection independently
of later BIOS header loads. ASIC version bits7/6 are 00 for domestic Japan,
10 for overseas USA and 11 for overseas Europe; the console keeps its existing
no-CD bit5. USA/Japan use the NTSC master clock53693175Hz, Europe53203424Hz.
SetRegionToPAL remains a compatibility helper selecting Europe or USA.

VDP status reads on the 68000 drive only bits0..9. Bits10..15 are taken from the
previous console bus word. Z80 status upper unused bits have their separate
pulled-high behavior. This is not a claim that every open-bus/prefetch waveform
or invalid-access lockup is fully modeled.

Primary hardware references:
Sega Genesis Software Manual, hardware version register and cartridge header:
https://segaretro.org/images/9/95/GenesisSoftwareManual.pdf
Hardware behavior documented by the Genesis Plus GX authors (facts only):
https://raw.githubusercontent.com/ekeeke/Genesis-Plus-GX/master/core/loadrom.c
https://raw.githubusercontent.com/ekeeke/Genesis-Plus-GX/master/core/vdp_ctrl.c

rom_loader_tests covers canonical selection, precedence, raw byte preservation,
invalid padding/data and BIOS handoff. console_tests checks the real ASIC read
lanes/master clocks and independently seeded VDP status open-bus values.
The Linux GUI region test executes original ROMs that only render when Japan,
hexadecimal Europe and multi-region USA bits survive the actual BIOS/main path:
  xvfb-run -a python3 UnitTests/frontend/test_linux_regions.py --emulator build/linux/DrivOnix
