"""Exercise the real frontend's argument/error paths without requiring a display."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--emulator", type=Path, required=True)
    args = parser.parse_args()
    emulator = str(args.emulator.resolve())
    environment = dict(os.environ)
    environment.pop("DISPLAY", None)
    environment.pop("WAYLAND_DISPLAY", None)

    with tempfile.TemporaryDirectory(prefix="drivonix frontend ") as temporary:
        directory = Path(temporary)

        def check(arguments, status, message):
            result = subprocess.run([emulator, *arguments], cwd=directory, env=environment,
                                    capture_output=True, text=True, timeout=10)
            output = result.stdout + result.stderr
            if result.returncode != status or message not in output:
                raise AssertionError(f"{arguments}: status={result.returncode}, output={output!r}")

        check(["--help"], 0, "Usage:")
        check(["-h"], 0, "file picker")
        check(["one.bin", "two.bin"], 2, "Usage:")
        check(["--unknown"], 2, "Usage:")
        check(["--no-audio", "missing.bin"], 1, "Cannot open ROM")
        check([""], 1, "ROM path is empty")
        check(["missing.bin"], 1, "Cannot open ROM")
        check(["no-extension"], 1, "Unsupported ROM extension")
        check(["unsupported.zip"], 1, "Unsupported ROM extension")
        (directory / "truncated.bin").write_bytes(b"short")
        check(["truncated.bin"], 1, "complete header")
        (directory / "partial.smd").write_bytes(bytes(513))
        check(["partial.smd"], 1, "complete 16 KiB blocks")
        (directory / "oversized.bin").write_bytes(bytes(0x800001))
        check(["oversized.bin"], 1, "8 MiB")
        if os.name != "nt":
            # A valid, uppercase filename in an unrelated working directory gets
            # through the real loader and asset lookup before rejecting no display.
            (directory / "valid ROM.BIN").write_bytes(bytes(0x400))
            check(["valid ROM.BIN"], 1, "X11 display")
            check([], 1, "Cannot open the ROM picker")
    print("Frontend arguments, invalid ROMs, and startup errors passed")


if __name__ == "__main__":
    main()
