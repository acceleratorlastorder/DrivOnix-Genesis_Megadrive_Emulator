"""Exercise the native archive chooser and real cartridge execution under Xvfb.

Both games are original synthetic cartridges. Selecting the wrong archive row
shows blue instead of red, so this test cannot pass by opening the first member.
"""
import argparse
import hashlib
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import zipfile

import test_linux_gui as gui


def stop(process):
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


def blue_cartridge():
    rom = gui.cartridge()
    instruction = bytes.fromhex("33fc000e00c00000")
    offset = rom.index(instruction)
    rom[offset + 2:offset + 4] = bytes.fromhex("0e00")
    checksum = sum(struct.unpack(f">{(len(rom) - 0x200) // 2}H", rom[0x200:])) & 0xFFFF
    struct.pack_into(">H", rom, 0x18E, checksum)
    return rom


def solid(screen, identifier, color):
    return all(screen.pixel(identifier, x, y) == color
               for x, y in ((20, 20), (160, 112), (300, 200)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emulator", type=Path, required=True)
    parser.add_argument("--report-dir", type=Path, default=Path("build/linux-archive-report"))
    args = parser.parse_args()
    emulator = str(args.emulator.resolve())
    report = args.report_dir.resolve()
    report.mkdir(parents=True, exist_ok=True)
    archiver = next((path for name in ("7zz", "7z", "7za")
                     if (path := shutil.which(name))), None)
    if not archiver:
        raise RuntimeError("The archive GUI test requires installed 7-Zip (7zz, 7z or 7za)")
    screen = gui.Screen()
    try:
        with tempfile.TemporaryDirectory(prefix="drivonix archive gui ") as temporary:
            directory = Path(temporary)
            environment = dict(gui.isolated_settings_environment(directory), LIBGL_ALWAYS_SOFTWARE="1",
                               GSETTINGS_BACKEND="memory", NO_AT_BRIDGE="1")
            members = {"01 blue.bin": blue_cartridge(),
                       "games/02 red [selected].bin": gui.cartridge(),
                       "readme.txt": b"Synthetic fixture; no commercial ROMs.\n"}
            archive_dir = directory / "archives with spaces"
            archive_dir.mkdir()
            archive_zip = archive_dir / "two games.zip"
            with zipfile.ZipFile(archive_zip, "w", compression=zipfile.ZIP_DEFLATED) as archive:
                for name, data in members.items():
                    archive.writestr(name, data)
            source = directory / "fixture source"
            for name, data in members.items():
                path = source / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
            archive_7z = archive_dir / "two games.7z"
            subprocess.run([archiver, "a", "-t7z", "-bd", "-y", str(archive_7z), *members],
                           cwd=source, env=environment, check=True, capture_output=True, timeout=30)
            archives = (archive_zip, archive_7z)
            original = {path: hashlib.sha256(path.read_bytes()).hexdigest() for path in archives}

            def launch(archive, log):
                return subprocess.Popen([emulator, "--no-audio", str(archive)], cwd=directory,
                                        env=environment, stdout=log, stderr=subprocess.STDOUT)

            # Cancellation must exit before loading a ROM or producing sidecar saves.
            with (report / "chooser-cancel.log").open("w") as log:
                process = launch(archive_zip, log)
                try:
                    chooser = gui.window(process, "^Select a game from archive$")
                    gui.key(chooser, "Escape")
                    assert process.wait(timeout=10) == 0, "Cancelling the archive chooser failed"
                    assert not list(directory.rglob("*.srm")), "Cancelled chooser wrote SRAM"
                    assert not list(directory.rglob("*.eep")), "Cancelled chooser wrote EEPROM"
                    assert set(archive_dir.iterdir()) == set(archives), "Cancelled chooser extracted files"
                finally:
                    stop(process)
            print("Archive chooser cancellation: clean exit, no ROM extraction or save writes", flush=True)

            for archive in archives:
                kind = archive.suffix[1:]
                with (report / f"{kind}-selected-game.log").open("w") as log:
                    process = launch(archive, log)
                    try:
                        chooser = gui.window(process, "^Select a game from archive$")
                        # The native tree starts at row 1. Choose row 2 and invoke
                        # its Open button, without any --archive-member argument.
                        gui.key(chooser, "Down", "alt+o")
                        identifier = gui.window(process, "^DrivOnix - Chili Hot Dog Version$")
                        gui.wait_for(process, lambda: solid(screen, identifier, (224, 0, 0)),
                                     f"{kind} second member red framebuffer after BIOS handoff", 90)
                        screen.save(identifier, report / f"{kind}-selected-red.png")
                        gui.xdo("windowfocus", "--sync", identifier)
                        gui.xdo("keydown", "--clearmodifiers", "a")
                        try:
                            gui.wait_for(process, lambda: solid(screen, identifier, (0, 224, 0)),
                                         f"{kind} selected member responds to player 1 Start", 10)
                        finally:
                            gui.xdo("keyup", "a")
                        screen.save(identifier, report / f"{kind}-selected-green.png")
                        gui.key(identifier, "Escape")
                        assert process.wait(timeout=10) == 0, f"{kind} cartridge did not exit cleanly"
                        assert set(archive_dir.iterdir()) == set(archives), "Archive loading extracted host files"
                        assert not (directory / "games").exists(), "Archive member directory escaped into cwd"
                        assert hashlib.sha256(archive.read_bytes()).hexdigest() == original[archive], \
                            "Opening a game modified its source archive"
                    finally:
                        stop(process)
                print(f"{kind}: native second-member choice, BIOS handoff, input and source preservation passed", flush=True)
    finally:
        screen.close()


if __name__ == "__main__":
    main()
