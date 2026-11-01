"""Prove hand-edited per-user INI settings affect the actual installed window."""
import argparse
from pathlib import Path
import subprocess
import tempfile

import test_linux_gui as gui


def geometry(identifier):
    fields = dict(line.split("=", 1) for line in gui.xdo("getwindowgeometry", "--shell", identifier).splitlines())
    return int(fields["WIDTH"]), int(fields["HEIGHT"])


def stop(process):
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--emulator", type=Path, required=True)
    parser.add_argument("--report-dir", type=Path, default=Path("build/linux-settings-report"))
    args = parser.parse_args()
    emulator = str(args.emulator.resolve())
    report = args.report_dir.resolve()
    report.mkdir(parents=True, exist_ok=True)
    screen = gui.Screen()
    try:
        with tempfile.TemporaryDirectory(prefix="drivonix settings gui ") as temporary:
            directory = Path(temporary)
            environment = dict(gui.isolated_settings_environment(directory), LIBGL_ALWAYS_SOFTWARE="1",
                               GSETTINGS_BACKEND="memory", NO_AT_BRIDGE="1")
            config = Path(environment["XDG_CONFIG_HOME"]) / "drivonix/drivonix.ini"
            config.parent.mkdir(parents=True)
            rom = directory / "settings-test.bin"
            rom.write_bytes(gui.cartridge())
            for scale in (2, 1):
                # A person edits the same per-user file between launches. Audio
                # is disabled through INI, without a --no-audio command override.
                original = (f"; retain this hand-written comment\n[display]\nscale={scale}\nfullscreen=false\n"
                            "[audio]\nenabled=false\nvolume=37\n[future]\nlayout=keep\n").encode()
                config.write_bytes(original)
                with (report / f"scale-{scale}.log").open("w") as log:
                    process = subprocess.Popen([emulator, str(rom)], cwd=directory, env=environment,
                                               stdout=log, stderr=subprocess.STDOUT)
                    try:
                        window = gui.window(process, "^DrivOnix - Chili Hot Dog Version$")
                        expected = (320 * scale, 224 * scale)
                        gui.wait_for(process, lambda: geometry(window) == expected,
                                     f"INI scale {scale} window size {expected}", 15)
                        gui.wait_for(process,
                                     lambda: screen.pixel(window, expected[0] // 2, expected[1] // 2) == (224, 0, 0),
                                     f"INI scale {scale} real BIOS/cartridge handoff", 90)
                        assert geometry(window) == expected, "VDP handoff discarded the INI window scale"
                        screen.save(window, report / f"scale-{scale}.png", *expected)
                        gui.key(window, "Escape")
                        assert process.wait(timeout=10) == 0, "Settings cartridge did not exit cleanly"
                        assert config.read_bytes() == original, "Normal exit rewrote a hand-edited INI"
                    finally:
                        stop(process)
                print(f"INI edit scale={scale}: {expected[0]}x{expected[1]} window, BIOS handoff and file preservation passed", flush=True)
    finally:
        screen.close()


if __name__ == "__main__":
    main()
