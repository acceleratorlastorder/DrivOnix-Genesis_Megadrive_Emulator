"""Real Linux frontend smoke test; run under xvfb-run with xdotool installed.

Generates a tiny original MC68000 cartridge. No commercial ROM is needed.
The ROM turns the screen red, then green when player 1 Start (A) is pressed.
"""
import argparse
import ctypes
import ctypes.util
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import time
import zlib


def cartridge():
    # The existing VDP maps a maximum CRAM channel (0xE) to RGB 224.
    rom = bytearray(0x4000)
    struct.pack_into(">II", rom, 0, 0x00FFFF00, 0x200)
    rom[0x100:0x110] = b"SEGA MEGA DRIVE "
    rom[0x120:0x150] = b"DRIVONIX LINUX INPUT TEST".ljust(48, b" ")
    rom[0x150:0x180] = rom[0x120:0x150]
    rom[0x1F0] = ord("U")
    code = bytearray.fromhex("46fc2700")  # Mask interrupts.
    for register in (0x8004, 0x8144, 0x8230, 0x832c, 0x8407, 0x856c,
                     0x8701, 0x8aff, 0x8c81, 0x8d3f, 0x8f02, 0x9001):
        code += struct.pack(">HHI", 0x33FC, register, 0xC00004)
    code += bytes.fromhex("23fcc002000000c00004 33fc000e00c00000")
    # Set TH as an output, drive it low, and poll active-low Start in bit 5.
    code += bytes.fromhex("13fc004000a10009 13fc000000a10003")
    code += bytes.fromhex("103900a10003 02000020 66f4")
    code += bytes.fromhex("23fcc002000000c00004 33fc00e000c00000 60fe")
    rom[0x200:0x200 + len(code)] = code
    checksum = sum(struct.unpack(f">{(len(rom) - 0x200) // 2}H", rom[0x200:])) & 0xFFFF
    struct.pack_into(">H", rom, 0x18E, checksum)
    return rom


def xdo(*arguments):
    return subprocess.check_output(["xdotool", *map(str, arguments)], text=True,
                                   stderr=subprocess.DEVNULL).strip()


def key(identifier, *keys):
    xdo("windowfocus", "--sync", identifier)
    xdo("key", "--clearmodifiers", "--delay", "100", *keys)


def wait_for(process, predicate, description, timeout=60):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise AssertionError(f"Emulator exited with {process.returncode} while waiting for {description}")
        value = predicate()
        if value:
            return value
        time.sleep(0.1)
    raise AssertionError(f"Timed out waiting for {description}")


def window(process, title):
    # CSFML 2 windows do not advertise _NET_WM_PID. This test runs on its own Xvfb display.
    def find():
        try:
            return int(xdo("search", "--onlyvisible", "--name", title).splitlines()[0])
        except (subprocess.CalledProcessError, IndexError):
            return None
    return wait_for(process, find, title, 15)


class Screen:
    def __init__(self):
        self.x = ctypes.CDLL(ctypes.util.find_library("X11"))
        self.x.XOpenDisplay.argtypes = [ctypes.c_char_p]
        self.x.XOpenDisplay.restype = ctypes.c_void_p
        self.x.XGetImage.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int, ctypes.c_int,
                                    ctypes.c_uint, ctypes.c_uint, ctypes.c_ulong, ctypes.c_int]
        self.x.XGetImage.restype = ctypes.c_void_p
        self.x.XGetPixel.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int]
        self.x.XGetPixel.restype = ctypes.c_ulong
        self.x.XDestroyImage.argtypes = [ctypes.c_void_p]
        self.x.XCloseDisplay.argtypes = [ctypes.c_void_p]
        self.display = self.x.XOpenDisplay(None)
        if not self.display:
            raise RuntimeError("Cannot open X11 display")

    def pixel(self, identifier):
        image = self.x.XGetImage(self.display, identifier, 160, 112, 1, 1, ctypes.c_ulong(-1), 2)
        if not image:
            raise AssertionError("Cannot capture emulator framebuffer")
        pixel = self.x.XGetPixel(image, 0, 0)
        self.x.XDestroyImage(image)
        return ((pixel >> 16) & 255, (pixel >> 8) & 255, pixel & 255)

    def save(self, identifier, path):
        image = self.x.XGetImage(self.display, identifier, 0, 0, 320, 224, ctypes.c_ulong(-1), 2)
        if not image:
            raise AssertionError("Cannot capture emulator framebuffer")
        rows = bytearray()
        for y in range(224):
            rows.append(0)
            for x in range(320):
                pixel = self.x.XGetPixel(image, x, y)
                rows.extend(((pixel >> 16) & 255, (pixel >> 8) & 255, pixel & 255))
        self.x.XDestroyImage(image)

        def chunk(kind, data):
            return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
        path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 320, 224, 8, 2, 0, 0, 0))
                         + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))

    def close(self):
        self.x.XCloseDisplay(self.display)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--emulator", type=Path, required=True)
    parser.add_argument("--report-dir", type=Path, default=Path("build/linux-gui-report"))
    args = parser.parse_args()
    emulator = str(args.emulator.resolve())
    report = args.report_dir.resolve()
    report.mkdir(parents=True, exist_ok=True)
    environment = dict(os.environ, LIBGL_ALWAYS_SOFTWARE="1", GSETTINGS_BACKEND="memory", NO_AT_BRIDGE="1")
    screen = Screen()
    with tempfile.TemporaryDirectory(prefix="drivonix gui ") as temporary:
        directory = Path(temporary)
        rom = directory / "input test.BIN"
        rom.write_bytes(cartridge())

        def launch(arguments, log_name):
            log = (report / log_name).open("w")
            process = subprocess.Popen([emulator, *arguments], cwd=directory, env=environment,
                                       stdout=log, stderr=subprocess.STDOUT)
            return process, log

        def finish(process, log):
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            log.close()

        # Cancelling a native picker is a successful exit, before any ROM load.
        process, log = launch([], "picker-cancel.log")
        try:
            identifier = window(process, "^Select a ROM$")
            key(identifier, "Escape")
            assert process.wait(timeout=10) == 0, "Cancelling the picker did not exit cleanly"
        finally:
            finish(process, log)
        print("Linux ROM picker cancellation passed", flush=True)

        # CLI path with spaces; exiting the BIOS must not start the cartridge.
        process, log = launch([str(rom)], "bios-exit.log")
        try:
            identifier = window(process, "^DrivOnix - Chili Hot Dog Version$")
            time.sleep(1)
            screen.save(identifier, report / "bios.png")
            key(identifier, "Escape")
            assert process.wait(timeout=10) == 0, "Escape during BIOS did not exit cleanly"
        finally:
            finish(process, log)
        print("Command-line ROM, BIOS rendering and BIOS exit passed", flush=True)

        # Open a real ROM through GTK; wait for the original BIOS to hand off.
        process, log = launch([], "rom-input.log")
        try:
            identifier = window(process, "^Select a ROM$")
            key(identifier, "ctrl+l")
            xdo("type", "--clearmodifiers", "--delay", "1", str(rom))
            key(identifier, "Return")
            time.sleep(0.5)
            # GTK may first resolve the typed location before activating Open.
            try:
                xdo("search", "--onlyvisible", "--name", "^Select a ROM$")
            except subprocess.CalledProcessError:
                pass
            else:
                key(identifier, "Return")
            identifier = window(process, "^DrivOnix - Chili Hot Dog Version$")
            wait_for(process, lambda: screen.pixel(identifier) == (224, 0, 0), "red cartridge framebuffer", 90)
            screen.save(identifier, report / "cartridge-red.png")
            xdo("windowfocus", "--sync", identifier)
            # Hold Start across emulated frames; a tap shorter than one frame can be missed.
            xdo("keydown", "--clearmodifiers", "a")
            try:
                wait_for(process, lambda: screen.pixel(identifier) == (0, 224, 0),
                         "green framebuffer after player 1 Start", 10)
            finally:
                xdo("keyup", "a")
            screen.save(identifier, report / "cartridge-green.png")
            key(identifier, "Escape")
            assert process.wait(timeout=10) == 0, "Escape during cartridge did not exit cleanly"
        finally:
            finish(process, log)
        print("GTK ROM selection, BIOS handoff, CPU/VDP rendering, joypad input and cartridge exit passed", flush=True)
    screen.close()


if __name__ == "__main__":
    main()
