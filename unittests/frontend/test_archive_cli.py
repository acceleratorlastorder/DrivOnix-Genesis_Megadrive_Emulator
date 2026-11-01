"""Exercise Unicode archive operands through the real frontend without a display."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import tempfile
import zipfile

from test_cli import isolated_settings_environment


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emulator", required=True, type=Path)
    args = parser.parse_args()
    emulator = str(args.emulator.resolve())
    with tempfile.TemporaryDirectory(prefix="drivonix archive cli ") as temporary:
        root = Path(temporary)
        environment = isolated_settings_environment(root)
        environment.pop("DISPLAY", None)
        environment.pop("WAYLAND_DISPLAY", None)
        archive = root / "collection 日本語.zip"
        member = "nested/日本語 second.smd"
        with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as output:
            output.writestr("first.bin", bytes(512))
            output.writestr(member, bytes(513))
            output.writestr("readme.txt", "Not a game")
        digest = hashlib.sha256(archive.read_bytes()).digest()

        def run(arguments, status, message=""):
            result = subprocess.run([emulator, *arguments], cwd=root, env=environment,
                                    capture_output=True, encoding="utf-8", timeout=15)
            assert result.returncode == status, (arguments, result.stdout, result.stderr)
            assert message in result.stdout + result.stderr, (arguments, result.stdout, result.stderr)
            return result

        unused = root / "must-not-create/settings.ini"
        listed = run(["--config", str(unused), "--list-archive", str(archive)], 0)
        assert set(listed.stdout.splitlines()) == {"first.bin", member}, listed.stdout
        assert not list(root.rglob("*.ini")), "Listing an archive wrote settings"
        run(["--archive-member", member, str(archive)], 1, "complete 16 KiB")
        run(["--archive-member", "missing.bin", str(archive)], 1, "not present in the archive")
        run(["--archive-member", "first.bin", str(root / "file.bin")], 1, "requires a ZIP or 7z")
        assert hashlib.sha256(archive.read_bytes()).digest() == digest
        assert not list(root.rglob("*.srm")) and not list(root.rglob("*.eep"))
        assert not list(root.rglob("*.bin")) and not list(root.rglob("*.smd")), "ROMs were extracted to disk"
    print("Frontend archive listing, exact Unicode member operands and errors passed")


if __name__ == "__main__":
    main()
