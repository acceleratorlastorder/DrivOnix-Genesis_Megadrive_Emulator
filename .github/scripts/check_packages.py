"""Extract release packages, verify their contents, and execute the packaged CLI."""
import argparse
from email.parser import Parser
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import subprocess
import sys
import tarfile
import tempfile
import zipfile


REPOSITORY = Path(__file__).resolve().parents[2]
DATA = Path("share/drivonix")


class PackageError(RuntimeError):
    pass


def require(condition, message):
    if not condition:
        raise PackageError(message)


def archive_target(directory, name, top):
    path = PurePosixPath(name)
    require(name and "\\" not in name and ":" not in name
            and name == name.lower()
            and not path.is_absolute() and ".." not in path.parts
            and path.parts and path.parts[0] == top,
            f"Unexpected archive path: {name!r}")
    return directory.joinpath(*path.parts)


def extract_archive(archive, directory, top):
    """Only regular files and directories belong in these relocatable archives."""
    if archive.name.endswith(".tar.gz"):
        with tarfile.open(archive, "r:gz") as package:
            for member in package.getmembers():
                target = archive_target(directory, member.name, top)
                require(member.isdir() or member.isfile(),
                        f"Unexpected archive entry type: {member.name}")
                if member.isdir():
                    target.mkdir(parents=True, exist_ok=True)
                else:
                    target.parent.mkdir(parents=True, exist_ok=True)
                    with package.extractfile(member) as source, target.open("wb") as output:
                        shutil.copyfileobj(source, output)
                    target.chmod(member.mode & 0o777)
    else:
        with zipfile.ZipFile(archive) as package:
            for member in package.infolist():
                target = archive_target(directory, member.filename, top)
                mode = member.external_attr >> 16
                require(not stat.S_ISLNK(mode), f"Unexpected ZIP link: {member.filename}")
                if member.is_dir():
                    target.mkdir(parents=True, exist_ok=True)
                else:
                    target.parent.mkdir(parents=True, exist_ok=True)
                    with package.open(member) as source, target.open("wb") as output:
                        shutil.copyfileobj(source, output)


def same_file(installed, source):
    require(installed.is_file(), f"Missing packaged file: {installed}")
    require(installed.read_bytes() == source.read_bytes(),
            f"Packaged file differs from source: {installed}")


def check_dependency_notices(installed_notices):
    notice_source = REPOSITORY / "thirdparty/windows-notices"
    same_file(installed_notices / "manifest.json", notice_source / "manifest.json")
    manifest = json.loads((notice_source / "manifest.json").read_text())
    for entry in manifest["files"]:
        installed = installed_notices / entry["path"]
        require(installed.is_file(), "Missing Windows dependency notice: " + entry["path"])
        require(hashlib.sha256(installed.read_bytes()).hexdigest() == entry["sha256"],
                "Windows dependency notice changed: " + entry["path"])


def check_layout(root, platform, version, commit):
    binary = root / "bin" / ("drivonix.exe" if platform == "windows" else "drivonix")
    require(binary.is_file() and binary.stat().st_size > 0, f"Missing packaged executable: {binary}")
    if platform == "linux":
        require(binary.stat().st_mode & 0o111, "Packaged Linux executable lost its execute permission")
    data = root / DATA
    for relative, original in (("bios/drivonix.bios", "bios/drivonix.bios"),
                               ("config/drivonix.ini", "config/drivonix.ini"),
                               ("readme.md", "readme.md"),
                               ("releasing.txt", "releasing.txt"),
                               ("settings.txt", "settings.txt"),
                               ("third-party/notice.txt", "thirdparty/notice.txt"),
                               ("third-party/sound-z80.lock.json", "thirdparty/sound-z80.lock.json")):
        same_file(data / relative, REPOSITORY / original)
    metadata_path = data / "build-info.json"
    require(metadata_path.is_file(), "Missing packaged build-info.json")
    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    expected = {"version": version, "commit": commit, "platform": platform,
                "build_os": "ubuntu-24.04" if platform == "linux" else "windows-2022"}
    for key, value in expected.items():
        require(metadata.get(key) == value, f"Packaged metadata {key} differs: {metadata.get(key)!r} != {value!r}")
    lock = json.loads((REPOSITORY / "thirdparty/sound-z80.lock.json").read_text())
    for entry in lock["files"]:
        relative = Path(entry["path"]).relative_to("thirdparty")
        installed = data / "third-party" / relative
        require(installed.is_file(), f"Missing packaged dependency source/license: {relative}")
        require(hashlib.sha256(installed.read_bytes()).hexdigest() == entry["sha256"],
                f"Packaged dependency source/license changed: {relative}")
    if platform == "windows":
        dlls = {path.name.lower() for path in (root / "bin").glob("*.dll")}
        required = {f"{prefix}-{component}-2.dll" for prefix in ("sfml", "csfml")
                    for component in ("graphics", "window", "system", "audio")}
        required.update(("openal32.dll", "msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll"))
        require(not required - dlls, "Missing Windows runtime DLLs: " + ", ".join(sorted(required - dlls)))
        licenses = data / "licenses"
        for dependency in ("sfml", "csfml"):
            require(any(path.is_file() and path.stat().st_size for path in licenses.glob(dependency + "-license.*")),
                    f"Missing {dependency} distribution license")
        check_dependency_notices(licenses / "sfml-dependencies")
    subprocess.run([sys.executable, str(REPOSITORY / "unittests/frontend/test_cli.py"),
                    "--emulator", str(binary)], check=True, timeout=180)


def check_debian(archive, directory, version):
    result = subprocess.run(["dpkg-deb", "--field", str(archive)], check=True,
                            capture_output=True, text=True)
    metadata = Parser().parsestr(result.stdout)
    require(metadata.get("Package") == "drivonix", "Unexpected Debian package identity")
    require(metadata.get("Architecture") == "amd64", "Debian package must target amd64")
    require(metadata.get("Version") == version.replace("-", "~", 1),
            "Debian package version differs from release")
    dependencies = metadata.get("Depends", "")
    for pattern, name in ((r"\blibc6\s*(?:\(|,|$)", "libc6"),
                          (r"\blibgtk-3-0(?:t64)?\s*(?:\(|,|$)", "GTK 3"),
                          (r"\blibcsfml-", "CSFML 2")):
        require(re.search(pattern, dependencies), f"Missing Debian {name} runtime dependency: {dependencies}")
    subprocess.run(["dpkg-deb", "--extract", str(archive), str(directory)], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--platform", choices=("linux", "windows"), required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--commit", required=True)
    args = parser.parse_args()
    require(re.fullmatch(r"[0-9][0-9A-Za-z.+-]*", args.version), "Unsafe package version")
    require(re.fullmatch(r"[0-9a-f]{40}", args.commit), "Expected full lowercase Git commit SHA")
    require(args.directory.is_dir(), "Package directory does not exist")
    stem = f"drivonix-{args.version}-{args.platform}-x86_64"
    suffixes = (".tar.gz", ".deb") if args.platform == "linux" else (".zip",)
    expected = {stem + suffix for suffix in suffixes}
    actual = {path.name for path in args.directory.iterdir()
              if any(path.name.endswith(suffix) for suffix in (".tar.gz", ".deb", ".zip"))}
    require(actual == expected, f"Package set differs: expected={sorted(expected)}, actual={sorted(actual)}")
    for suffix in suffixes:
        archive = (args.directory / (stem + suffix)).resolve()
        with tempfile.TemporaryDirectory(prefix="drivonix package ") as temporary:
            extracted = Path(temporary)
            if suffix == ".deb":
                check_debian(archive, extracted, args.version)
                root = extracted / "usr"
            else:
                extract_archive(archive, extracted, stem)
                root = extracted / stem
            check_layout(root, args.platform, args.version, args.commit)
        print(f"Verified contents and real executable: {archive.name}", flush=True)


if __name__ == "__main__":
    try:
        main()
    except (PackageError, OSError, ValueError, KeyError, tarfile.TarError,
            zipfile.BadZipFile, subprocess.SubprocessError) as error:
        print(f"Package verification failed: {error}", file=sys.stderr)
        raise SystemExit(1)
