"""Exercise real BIOS/main region handoff under Xvfb using original ROMs."""
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile

import test_linux_gui as gui


def cartridge(field, expected_bits):
    rom = gui.cartridge()
    original = rom[0x200:0x400]
    # Read the hardware version, mask overseas/PAL bits and compare independently.
    # An incorrect region loops forever before the original ROM can render red.
    prefix = (bytes.fromhex("103900a10001 020000c0")
              + struct.pack(">HH", 0x0C00, expected_bits)
              + bytes.fromhex("6702 60fe"))
    rom[0x200:0x200 + len(prefix) + len(original)] = prefix + original
    rom[0x1F0:0x200] = field.ljust(16, b" ")
    checksum = sum(struct.unpack(f">{(len(rom) - 0x200) // 2}H", rom[0x200:])) & 0xFFFF
    struct.pack_into(">H", rom, 0x18E, checksum)
    return rom


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--emulator", type=Path, required=True)
    parser.add_argument("--report-dir", type=Path, default=Path("build/linux-region-report"))
    args = parser.parse_args()
    emulator = str(args.emulator.resolve())
    report = args.report_dir.resolve()
    report.mkdir(parents=True, exist_ok=True)
    screen = gui.Screen()
    try:
        with tempfile.TemporaryDirectory(prefix="drivonix region ") as temporary:
            environment = dict(gui.isolated_settings_environment(Path(temporary)), LIBGL_ALWAYS_SOFTWARE="1",
                               GSETTINGS_BACKEND="memory", NO_AT_BRIDGE="1")
            for field, expected in ((b"J", 0), (b"8", 0xC0), (b"JUE", 0x80)):
                name = field.decode("ascii").lower()
                rom = Path(temporary) / f"{name}.bin"
                rom.write_bytes(cartridge(field, expected))
                with (report / f"{name}.log").open("w") as log:
                    process = subprocess.Popen([emulator, "--no-audio", str(rom)], cwd=temporary,
                                               env=environment, stdout=log, stderr=subprocess.STDOUT)
                    try:
                        window = gui.window(process, "^DrivOnix - Chili Hot Dog Version$")
                        gui.wait_for(process, lambda: screen.pixel(window) == (224, 0, 0),
                                     f"{name} BIOS handoff and hardware region bits {expected:02X}", 90)
                        screen.save(window, report / f"{name}.png")
                        gui.key(window, "Escape")
                        assert process.wait(timeout=10) == 0, "Region cartridge did not exit cleanly"
                    finally:
                        if process.poll() is None:
                            process.terminate()
                            try:
                                process.wait(timeout=5)
                            except subprocess.TimeoutExpired:
                                process.kill()
                                process.wait()
                print(f"{name}: BIOS/main handoff and hardware region bits {expected:02X} passed", flush=True)
    finally:
        screen.close()


if __name__ == "__main__":
    main()
