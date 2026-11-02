"""Real retro frontend acceptance under Xvfb; uses generated original ROMs only.

Requires xdotool. Settings, SQLite state, artwork caches and battery saves are
isolated from the real profile and Downloads. Screenshots and a JSON report are
retained for review, including a failure screenshot if an assertion fails.
"""
import argparse
import configparser
import json
import os
from pathlib import Path
import shutil
import sqlite3
import struct
import subprocess
import tempfile
import time
import zipfile

import test_linux_gui as gui


RED, GREEN, BLUE = (224, 0, 0), (0, 224, 0), (0, 0, 224)
YELLOW = (224, 224, 0)


def battery_cartridge(region="U"):
    """Red until Start, then green/write SRAM; later boots turn saved SRAM blue."""
    rom = gui.cartridge()
    setup = bytes.fromhex("13fc004000a10009 13fc000000a10003")
    poll = bytes.fromhex("103900a10003 02000020 66f4")
    green = bytes.fromhex("23fcc002000000c00004 33fc00e000c00000")
    write = bytes.fromhex("13fc005a00200001 60fe")
    fresh = setup + poll + green + write
    saved = bytes.fromhex("23fcc002000000c00004 33fc0e0000c00000 60fe")
    offset = rom.index(setup, 0x200)
    # CMPI.B #$5a,$200001; BEQ saved; otherwise preserve the original input ROM.
    code = bytes.fromhex("0c39005a00200001") + bytes((0x67, len(fresh))) + fresh + saved
    rom[offset:offset + len(code)] = code
    rom[0x1B0:0x1B2] = b"RA"
    struct.pack_into(">II", rom, 0x1B4, 0x200001, 0x2003FF)
    rom[0x1F0] = ord(region)
    checksum = sum(struct.unpack(f">{(len(rom) - 0x200) // 2}H", rom[0x200:])) & 0xFFFF
    struct.pack_into(">H", rom, 0x18E, checksum)
    return rom


def pal_cartridge():
    """Yellow only when the actual ASIC exposes PAL; magenta otherwise."""
    rom = gui.cartridge()
    setup = bytes.fromhex("13fc004000a10009 13fc000000a10003")
    offset = rom.index(setup, 0x200)
    yellow = bytes.fromhex("23fcc002000000c00004 33fc00ee00c00000 60fe")
    magenta = bytes.fromhex("23fcc002000000c00004 33fc0e0e00c00000 60fe")
    code = bytes.fromhex("103900a10001 02000040") + bytes((0x67, len(yellow))) + yellow + magenta
    rom[offset:offset + len(code)] = code
    rom[0x1F0] = ord("E")
    checksum = sum(struct.unpack(f">{(len(rom) - 0x200) // 2}H", rom[0x200:])) & 0xFFFF
    struct.pack_into(">H", rom, 0x18E, checksum)
    return rom


def geometry(window):
    fields = dict(line.split("=", 1) for line in gui.xdo("getwindowgeometry", "--shell", window).splitlines())
    return int(fields["WIDTH"]), int(fields["HEIGHT"])


class Acceptance:
    def __init__(self, emulator, report, root):
        self.emulator, self.report, self.root = emulator, report, root
        self.screen = gui.Screen()
        self.process = self.log = self.window = None
        self.events = []
        self.environment = dict(gui.isolated_settings_environment(root),
                                XDG_DATA_HOME=str(root / "data"), XDG_CACHE_HOME=str(root / "cache"),
                                LIBGL_ALWAYS_SOFTWARE="1", ALSOFT_DRIVERS="null",
                                GSETTINGS_BACKEND="memory", NO_AT_BRIDGE="1")
        self.config = Path(self.environment["XDG_CONFIG_HOME"]) / "drivonix/drivonix.ini"
        self.database = Path(self.environment["XDG_DATA_HOME"]) / "drivonix/library.sqlite"
        self.games = Path(self.environment["HOME"]) / "games"
        self.games.mkdir(parents=True)
        self.raw = self.games / "Frontend Test (U) [!].bin"
        self.raw.write_bytes(battery_cartridge())
        self.raw_save = Path(str(self.raw) + ".srm")
        self.raw_save.write_bytes(bytes([0xFF]) * 512)
        (Path(self.environment["HOME"]) / "broken.bin").write_bytes(b"incomplete")
        self.archive = self.games / "Frontend Test.zip"
        with zipfile.ZipFile(self.archive, "w", zipfile.ZIP_DEFLATED) as archive:
            archive.writestr(self.raw.name, self.raw.read_bytes())
            archive.writestr("Frontend Test (E) [!].bin", pal_cartridge())
            archive.writestr("Unsupported Huge (U).bin", bytes(0x800001))
        self.config.parent.mkdir(parents=True)
        self.config.write_text("; preserve my manual comment\n[display]\nscale=1\ninteger_fit=true\nfullscreen=false\n"
                               "[audio]\nenabled=false\nvolume=37\n[interface]\nshow_bios=false\n"
                               "window_width=960\nwindow_height=540\n[metadata]\nonline=false\n"
                               "[future]\nkeep=this value\n", encoding="utf-8")

    def launch(self, label, expected_geometry=(960, 540)):
        self.log = (self.report / f"{label}.log").open("w", encoding="utf-8")
        self.process = subprocess.Popen([self.emulator], cwd=self.root, env=self.environment,
                                        stdout=self.log, stderr=subprocess.STDOUT)
        self.window = gui.window(self.process, "^DrivOnix ")
        gui.xdo("windowfocus", "--sync", self.window)
        if expected_geometry:
            self.wait(lambda: geometry(self.window) == expected_geometry, "configured frontend geometry")
        time.sleep(0.3)

    def wait(self, predicate, label, timeout=20):
        return gui.wait_for(self.process, predicate, label, timeout)

    def click(self, x, y, settle=0.2):
        width, height = geometry(self.window)
        scale = min(width / 640, height / 360)
        px = round((width - 640 * scale) / 2 + x * scale)
        py = round((height - 360 * scale) / 2 + y * scale)
        gui.xdo("windowfocus", "--sync", self.window)
        gui.xdo("mousemove", "--window", self.window, px, py)
        gui.xdo("click", "1")
        time.sleep(settle)

    def key(self, *keys):
        gui.key(self.window, *keys)
        time.sleep(0.15)

    def hold(self, key, seconds=0.3):
        gui.xdo("windowfocus", "--sync", self.window)
        gui.xdo("keydown", "--clearmodifiers", key)
        try:
            time.sleep(seconds)
        finally:
            gui.xdo("keyup", key)
        time.sleep(0.15)

    def title(self):
        return gui.xdo("getwindowname", self.window)

    def pixel(self):
        width, height = geometry(self.window)
        return self.screen.pixel(self.window, width // 2, height // 2)

    def edge_pixel(self):
        width, height = geometry(self.window)
        return self.screen.pixel(self.window, width // 2 + 180, height // 2)

    def shot(self, name):
        width, height = geometry(self.window)
        self.screen.save(self.window, self.report / f"{name}.png", width, height)

    def sql(self, query, parameters=()):
        if not self.database.exists():
            return []
        try:
            with sqlite3.connect(self.database.as_uri() + "?mode=ro", uri=True, timeout=1) as connection:
                return connection.execute(query, parameters).fetchall()
        except sqlite3.OperationalError:
            return []

    def values(self):
        config = configparser.ConfigParser()
        config.read(self.config, encoding="utf-8-sig")
        return config

    def record(self, label, **data):
        self.events.append(dict(check=label, **data))
        (self.report / "report.json").write_text(json.dumps(self.events, indent=2) + "\n")
        print(label + " passed", flush=True)

    def finish(self):
        if self.process and self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
        if self.log:
            self.log.close()
        self.process = self.log = None

    def quit(self):
        self.click(605, 19)
        assert self.process.wait(timeout=15) == 0, "Quit did not close cleanly"
        self.finish()

    def run(self):
        self.launch("frontend-first")
        self.shot("01-home-empty")
        self.record("real home screen", geometry=geometry(self.window))
        # Add an actual folder through the native app's own browser, not by
        # pre-populating its settings or SQLite index.
        self.click(475, 58)
        self.click(250, 88)  # HOME contains the generated games folder only.
        self.click(195, 316)
        self.wait(lambda: self.sql("SELECT COUNT(*) FROM sources") == [(4,)], "indexed raw/archive copies")
        self.wait(lambda: any(name.startswith("library.") for name in self.values().sections()), "folder settings saved")
        self.shot("02-home-indexed")
        self.record("folder browser and real indexing", copies=4, variants=self.sql("SELECT COUNT(*) FROM variants")[0][0])
        self.click(62, 134)
        self.click(315, 317)
        self.wait(lambda: self.sql("SELECT COUNT(*) FROM games WHERE favorite=1") == [(1,)], "favorite persistence")
        self.shot("03-variants")
        self.click(390, 89)
        self.shot("04-source-copies")
        self.click(310, 155)  # Second row is the archived U copy, not raw BIN.
        self.wait(lambda: self.pixel() == RED and "[Paused]" not in self.title(), "red archive cartridge", 30)
        self.shot("05-game-red")
        # Menu input must not leak into the paused emulated controller.
        self.key("Escape")
        self.wait(lambda: "[Paused]" in self.title(), "pause menu")
        self.shot("06-paused")
        self.hold("a")
        self.click(320, 126)
        self.wait(lambda: "[Paused]" not in self.title() and self.pixel() == RED, "neutral resume without leaked Start")
        self.hold("a")
        self.wait(lambda: self.pixel() == GREEN, "fresh Start reaches the emulated joypad")
        self.shot("07-game-green")
        self.record("archive play, pause isolation and keyboard input", pixel=self.pixel())
        # Change an actual setting and bind an unused key through real capture.
        self.key("Escape")
        self.wait(lambda: "[Paused]" in self.title(), "pause before settings")
        self.click(320, 187)
        self.shot("08-settings-display")
        self.click(166, 60)
        self.click(110, 225)
        self.click(140, 318)
        self.wait(lambda: self.values().getint("audio", "volume") == 32, "UI audio value saved")
        self.click(270, 60)
        self.click(310, 232)
        self.click(140, 212)
        time.sleep(0.25)
        self.key("t")
        self.shot("09-keyboard-binding")
        self.click(500, 320)
        self.click(140, 318)
        self.wait(lambda: self.values().get("keyboard.player1", "start").strip('"') == "t", "captured binding saved")
        saved_settings = self.config.read_bytes()
        assert b"preserve my manual comment" in saved_settings and b"keep=this value" in saved_settings
        self.click(500, 318)
        self.click(320, 253)
        self.wait(lambda: "[Paused]" not in self.title() and "INPUT TEST" not in self.title(), "return to library")
        self.wait(lambda: bool(list(self.games.glob("*.member-*.srm"))), "archive battery save")
        saves = list(self.games.glob("*.member-*.srm"))
        assert len(saves) == 1 and saves[0].read_bytes() == b"\x5a" + b"\xff" * 511
        assert self.raw_save.read_bytes() == b"\xff" * 512, "Archive copied over the raw game's separate save"
        self.wait(lambda: self.sql("SELECT SUM(seconds) FROM sessions")[0][0] > 0, "active play history")
        self.record("UI settings, remapping, preserved INI and isolated battery save", save=saves[0].name)
        # Exact archive selection retains unsupported siblings as diagnostics.
        self.click(360, 19)
        self.click(250, 88)
        self.click(250, 109)
        time.sleep(0.25)
        self.shot("10-exact-archive-members")
        self.click(310, 137)
        self.shot("11-unsupported-member")
        self.key("Escape")
        self.click(575, 318)
        self.quit()
        self.launch("frontend-reopen")
        self.wait(lambda: self.sql("SELECT COUNT(*) FROM games WHERE favorite=1") == [(1,)], "favorite after reopen")
        assert self.values().getint("audio", "volume") == 32
        assert self.values().get("keyboard.player1", "start").strip('"') == "t"
        self.shot("12-home-reopened")
        self.click(62, 134)
        self.click(390, 89)
        self.click(310, 155)
        self.wait(lambda: self.pixel() == BLUE, "reopened archived game reads its previous battery memory", 30)
        self.shot("13-battery-reloaded")
        self.key("Escape")
        self.click(320, 154)
        self.wait(lambda: self.pixel() == BLUE and "[Paused]" not in self.title(), "restart retains battery memory")
        # A failed switch must preserve the paused game and its saved memory.
        self.key("Escape")
        self.click(320, 220)
        self.click(250, 109)  # Broken ROM outside the configured library root.
        self.shot("14-failed-switch")
        self.key("Escape")
        self.click(320, 121)
        self.wait(lambda: self.pixel() == BLUE and "[Paused]" not in self.title(), "failed switch retains resumable cartridge")
        # Whole-pixel fit and explicit fixed scale must affect actual game pixels.
        assert self.edge_pixel() == BLUE, "Integer fit did not fill the expected 2x image"
        self.key("Escape")
        self.click(320, 187)
        self.click(470, 111)
        self.click(140, 318)
        self.wait(lambda: not self.values().getboolean("display", "integer_fit"), "fixed scale setting")
        self.click(500, 318)
        self.click(320, 121)
        self.wait(lambda: self.pixel() == BLUE and self.edge_pixel() == (0, 0, 0), "fixed 1x rendering with black border")
        self.shot("15-fixed-scale")
        self.key("Escape")
        self.click(320, 187)
        self.click(470, 111)
        self.click(140, 318)
        self.click(500, 318)
        self.click(320, 121)
        self.wait(lambda: self.edge_pixel() == BLUE, "integer fit restored")
        self.key("F11")
        self.window = gui.window(self.process, "^DrivOnix ")
        self.wait(lambda: geometry(self.window) == (1280, 720) and self.pixel() == BLUE, "fullscreen retains cartridge")
        self.shot("16-fullscreen")
        self.key("F11")
        self.window = gui.window(self.process, "^DrivOnix ")
        self.wait(lambda: geometry(self.window) == (960, 540) and self.pixel() == BLUE, "windowed mode restored")
        gui.xdo("windowsize", self.window, 500, 250)
        self.wait(lambda: geometry(self.window) == (640, 360), "minimum frontend size")
        self.shot("17-minimum-window")
        gui.xdo("windowsize", self.window, 960, 540)
        self.wait(lambda: geometry(self.window) == (960, 540), "frontend resize recovery")
        self.record("integer/fixed scaling, fullscreen, minimum size and failed-switch preservation")
        self.key("Escape")
        self.click(320, 253)
        self.wait(lambda: "INPUT TEST" not in self.title(), "return before final quit")
        self.click(62, 134)
        self.click(390, 114)  # Exact European variant uses the actual PAL ASIC bit.
        self.wait(lambda: self.pixel() == YELLOW, "European cartridge observes PAL hardware", 30)
        self.shot("18-pal-hardware")
        self.key("Escape")
        self.click(320, 253)
        # Optional original BIOS, explicitly enabled through the real settings.
        self.click(533, 19)
        self.click(465, 149)
        self.click(140, 318)
        self.wait(lambda: self.values().getboolean("interface", "show_bios"), "BIOS preference saved")
        self.click(500, 318)
        self.click(62, 134)
        self.click(390, 89)
        self.click(310, 155)
        self.wait(lambda: "INPUT TEST" in self.title() and "[Paused]" not in self.title(), "optional BIOS starts", 30)
        assert self.pixel() != BLUE, "BIOS preference was ignored"
        self.shot("19-original-bios")
        self.click(320, 330)
        self.wait(lambda: self.pixel() == BLUE, "optional BIOS skip enters saved cartridge", 30)
        self.key("Escape")
        self.click(320, 253)
        saved_settings = self.config.read_bytes()
        self.quit()
        assert self.config.read_bytes() == saved_settings, "Ordinary close/reopen rewrote saved settings"
        assert saves[0].read_bytes() == b"\x5a" + b"\xff" * 511
        assert self.raw_save.read_bytes() == b"\xff" * 512
        shutil.copy2(self.config, self.report / "saved-drivonix.ini")
        shutil.copy2(saves[0], self.report / "archive-battery.srm")
        with sqlite3.connect(self.database) as source, sqlite3.connect(self.report / "library.sqlite") as target:
            source.backup(target)
        self.record("close/reopen, recent history, remapped settings and SRAM restart", favorite=True,
                    session_rows=self.sql("SELECT COUNT(*) FROM sessions")[0][0])
        self.record("PAL ASIC selection and optional original BIOS skip")

    def read_only_settings(self):
        original = self.config.read_bytes()
        self.config.parent.chmod(0o555)
        try:
            self.launch("readonly-settings")
            self.click(533, 19)
            self.click(166, 60)
            self.click(110, 225)
            self.click(140, 318)
            self.shot("readonly-settings-error")
            assert self.config.read_bytes() == original, "Failed UI save replaced read-only settings"
            self.config.parent.chmod(0o700)
            self.click(100, 260)
            self.wait(lambda: self.values().getint("audio", "volume") == 32, "retry retains pending settings")
            self.shot("readonly-settings-retry")
            # Exit whichever safe settings/error page the retry returns to.
            self.key("Escape")
            self.key("Escape")
            self.click(605, 19)
            assert self.process.wait(timeout=15) == 0
            self.finish()
            self.record("read-only settings failure preserves file and pending retry")
        finally:
            self.config.parent.chmod(0o700)

    def migration(self):
        self.config.unlink()
        legacy = self.root / "Config/config.txt"
        legacy.parent.mkdir()
        original = b"; legacy file remains untouched\nScreenSize=2;\n"
        legacy.write_bytes(original)
        # Defaults enable online metadata; keep this migration check offline
        # even before the first INI has been generated.
        for name in ("HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY", "http_proxy", "https_proxy", "all_proxy"):
            self.environment[name] = "http://127.0.0.1:9"
        self.launch("legacy-migration", expected_geometry=None)
        self.wait(lambda: self.config.exists(), "legacy settings migrated")
        assert self.values().getint("display", "scale") == 2
        assert not self.values().getboolean("display", "fullscreen")
        assert legacy.read_bytes() == original
        self.shot("legacy-migrated-home")
        self.quit()
        self.record("real GUI legacy migration retains original file")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emulator", type=Path, required=True)
    parser.add_argument("--report-dir", type=Path, default=Path("build/linux-frontend-report"))
    args = parser.parse_args()
    report = args.report_dir.resolve()
    report.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="drivonix frontend acceptance ") as temporary:
        acceptance = Acceptance(str(args.emulator.resolve()), report, Path(temporary))
        try:
            acceptance.run()
        except BaseException:
            if acceptance.process and acceptance.process.poll() is None:
                try:
                    acceptance.shot("failure")
                except Exception:
                    pass
            raise
        finally:
            acceptance.finish()
            acceptance.screen.close()
    for label, operation in (("readonly", "read_only_settings"), ("legacy", "migration")):
        with tempfile.TemporaryDirectory(prefix=f"drivonix frontend {label} ") as temporary:
            acceptance = Acceptance(str(args.emulator.resolve()), report, Path(temporary))
            try:
                getattr(acceptance, operation)()
            except BaseException:
                if acceptance.process and acceptance.process.poll() is None:
                    try:
                        acceptance.shot(f"failure-{label}")
                    except Exception:
                        pass
                raise
            finally:
                acceptance.finish()
                acceptance.screen.close()


if __name__ == "__main__":
    main()
