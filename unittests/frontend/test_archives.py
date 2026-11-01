"""Exercise native archive loading using generated cartridges, not game ROMs."""
import argparse
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile


def cartridge():
    data = bytearray((i * 37) & 255 for i in range(0x8000))
    data[0x100:0x110] = b"SEGA MEGA DRIVE ".ljust(16)
    data[0x1F0:0x200] = b"U               "
    checksum = sum(struct.unpack(">" + "H" * ((len(data) - 0x200) // 2), data[0x200:])) & 65535
    data[0x18E:0x190] = struct.pack(">H", checksum)
    return bytes(data)


def tool():
    for name in ("7zz", "7z", "7za"):
        found = shutil.which(name)
        if found:
            return found
    if os.name == "nt":
        for name in ("ProgramW6432", "ProgramFiles", "ProgramFiles(x86)"):
            candidate = Path(os.environ.get(name, "")) / "7-Zip/7z.exe"
            if candidate.is_file():
                return str(candidate)
    raise RuntimeError("Archive tests require installed 7-Zip (7zz, 7z or 7za)")


class Archives(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="drivonix archives ")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.image = cartridge()
        self.raw = self.root / "baseline 日本語.bin"
        self.raw.write_bytes(self.image)

    def probe(self, *arguments, error=None, env=None):
        result = subprocess.run([PROBE, *map(str, arguments)], capture_output=True,
                                text=True, encoding="utf-8", errors="replace", timeout=40, env=env)
        if error is None:
            self.assertEqual(result.returncode, 0, result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, "Invalid archive unexpectedly accepted")
            self.assertIn(error, result.stderr)
        return result.stdout

    def archive(self, entries, name="games.zip", compression=zipfile.ZIP_DEFLATED):
        path = self.root / name
        with zipfile.ZipFile(path, "w", compression=compression) as archive:
            for member, data in entries:
                archive.writestr(member, data)
        return path

    def seven(self, path, source, *options):
        subprocess.run([SEVEN, "a", "-bd", "-bb0", *options, str(path), str(source)],
                       check=True, capture_output=True, timeout=30)

    def test_single_zip_unicode_paths_and_literal_arguments(self):
        path = self.archive([("folder/日本語 sonic [!];'&$().BIN", self.image)],
                            "日本語 space &'$() [set].ZIP")
        before = set(self.root.iterdir())
        self.probe("read", path, "", self.raw)
        self.probe("load", path, "", self.raw)
        self.assertEqual(set(self.root.iterdir()), before, "Archive loader extracted files to disk")
        self.assertIn("folder/日本語 sonic [!];'&$().BIN", self.probe("list", path))

    def test_member_selection_and_disabled_wildcards(self):
        members = ["-leading-dash.bin", "literal[?]*.bin", "nested/other.md", "case.bin", "CASE.BIN"]
        path = self.archive([(name, self.image) for name in members])
        self.probe("read", path, "", self.raw, error="multiple games")
        for member in members:
            self.probe("read", path, member, self.raw)
        self.probe("read", path, "nested\\other.md", self.raw)
        self.probe("read", path, "missing.bin", self.raw, error="not present")

    def test_7z_and_smd_decoding(self):
        source = self.root / "source 日本語.smd"
        encoded = bytearray(512)
        for offset in range(0, len(self.image), 16384):
            block = self.image[offset:offset + 16384]
            encoded.extend(block[1::2] + block[0::2])
        source.write_bytes(encoded)
        path = self.root / "cartridge 日本語.7z"
        self.seven(path, source, "-t7z")
        self.probe("read", path, "", source)
        self.probe("load", path, "", self.raw)

    def test_invalid_archives_and_cartridge_limits(self):
        path = self.archive([("readme.txt", b"no game")])
        self.probe("list", path, error="no supported")
        path = self.archive([("tiny.bin", b"tiny")], "tiny.zip")
        self.probe("list", path, error="complete header")
        path = self.archive([("large.bin", bytes(0x800000 + 513))], "large.zip")
        self.probe("list", path, error="8 MiB")
        path = self.archive([("normal.bin", self.image)], "truncated.zip")
        path.write_bytes(path.read_bytes()[:len(path.read_bytes()) // 2])
        self.probe("list", path, error="could not read")
        path = self.root / "corrupt.7z"
        path.write_bytes(b"not an archive")
        self.probe("list", path, error="could not read")

    def test_crc_failure_is_rejected_after_full_output(self):
        path = self.archive([("normal.bin", self.image)], compression=zipfile.ZIP_STORED)
        data = bytearray(path.read_bytes())
        name_size, extra_size = struct.unpack_from("<HH", data, 26)
        data[30 + name_size + extra_size + 0x250] ^= 1
        path.write_bytes(data)
        self.probe("read", path, "normal.bin", self.raw, error="could not read")

    def test_encrypted_files_and_headers(self):
        path = self.root / "encrypted.zip"
        self.seven(path, self.raw, "-tzip", "-psecret")
        self.probe("list", path, error="Encrypted")
        path = self.root / "encrypted.7z"
        self.seven(path, self.raw, "-t7z", "-psecret", "-mhe=on")
        self.probe("list", path, error="encrypted")

    def test_unsafe_duplicate_and_link_members(self):
        for index, member in enumerate(("../escape.bin", "/escape.bin", "C:\\escape.bin",
                                        "nested/../escape.bin", "nested\\..\\escape.bin", "@list.bin")):
            with self.subTest(member=member):
                path = self.archive([(member, self.image)], f"unsafe{index}.zip")
                self.probe("list", path, error="unsafe")
        # 7-Zip replaces console control characters with underscores in its
        # listing. An exact extraction must still reject that lossy name.
        path = self.archive([("newline\nname.bin", self.image)], "newline.zip")
        self.probe("read", path, "", self.raw, error="incomplete")
        with self.assertWarns(UserWarning):
            path = self.archive([("duplicate.bin", self.image), ("duplicate.bin", self.image)], "duplicate.zip")
        self.probe("list", path, error="duplicate")
        path = self.root / "link.zip"
        with zipfile.ZipFile(path, "w") as archive:
            member = zipfile.ZipInfo("link.bin")
            member.create_system = 3
            member.external_attr = 0o120777 << 16
            archive.writestr(member, b"target.bin")
        self.probe("list", path, error="links")
        self.assertFalse((self.root.parent / "escape.bin").exists())

    def test_member_save_isolation_and_unicode_persistence(self):
        path = self.archive([("game.bin", self.image)], "保存 日本語.zip")
        self.probe("save", path)
        saves = list(self.root.glob("*.srm"))
        self.assertEqual(len(saves), 2)
        self.assertEqual({save.read_bytes()[0] for save in saves}, {0x35, 0x79})
        self.assertTrue(all(save.stat().st_size == 128 for save in saves))

    def test_process_limits_and_no_shell(self):
        self.probe("process", sys.executable)

    def test_missing_dependency_has_actionable_error(self):
        environment = os.environ.copy()
        environment["PATH"] = os.pathsep.join(directory for directory in environment.get("PATH", "").split(os.pathsep)
                                             if directory and not any((Path(directory) / name).is_file()
                                                                      for name in ("7z", "7zz", "7za", "7z.exe", "7zz.exe", "7za.exe")))
        for name in ("ProgramW6432", "ProgramFiles", "ProgramFiles(x86)"):
            environment[name] = str(self.root / "not installed")
        path = self.archive([("normal.bin", self.image)])
        self.probe("list", path, error="Install 7-Zip", env=environment)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", required=True, type=Path)
    args, remaining = parser.parse_known_args()
    PROBE = str(args.probe.resolve())
    SEVEN = tool()
    unittest.main(argv=[sys.argv[0], *remaining])
