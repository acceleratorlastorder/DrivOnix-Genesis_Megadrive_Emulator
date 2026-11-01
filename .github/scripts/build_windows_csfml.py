"""Build pinned native 64-bit Windows dependencies with the same MSVC toolchain."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tarfile
import urllib.request


SOURCES = (
    ("SFML", "2.6.2", "15ff4d608a018f287c6a885db0a2da86ea389e516d2323629e4d4407a7ce047f"),
    ("CSFML", "2.6.1", "f3f3980f6b5cad85b40e3130c10a2ffaaa9e36de5f756afd4aacaed98a7a9b7b"),
)


def copy_dependency_licenses(source, licenses):
    # SFML 2.6.2 does not contain extlibs/licenses. Preserve the full upstream
    # terms and the original header notices from the pinned source instead.
    bundle = Path(__file__).resolve().parents[2] / "thirdparty/windows-notices"
    manifest = json.loads((bundle / "manifest.json").read_text())
    digest = next(digest for name, _, digest in SOURCES if name == "SFML")
    if manifest["sfml_archive_sha256"] != digest:
        raise RuntimeError("Windows dependency notices do not match the pinned SFML archive")
    for entry in manifest["files"]:
        data = (bundle / entry["path"]).read_bytes()
        if hashlib.sha256(data).hexdigest() != entry["sha256"]:
            raise RuntimeError("Windows dependency notice changed: " + entry["path"])
        if "sfml_path" in entry:
            original = (source / entry["sfml_path"]).read_bytes()
            if hashlib.sha256(original).hexdigest() != entry["sfml_sha256"]:
                raise RuntimeError("SFML header differs from its retained notice: " + entry["sfml_path"])
    shutil.copytree(bundle, licenses / "sfml-dependencies", dirs_exist_ok=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--prefix", type=Path, required=True)
    args = parser.parse_args()
    prefix = args.prefix.resolve()
    workspace = prefix.parent / "dependency-sources"
    workspace.mkdir(parents=True, exist_ok=True)
    licenses = prefix / "licenses"
    licenses.mkdir(parents=True, exist_ok=True)
    for name, version, digest in SOURCES:
        data = urllib.request.urlopen(
            f"https://codeload.github.com/SFML/{name}/tar.gz/refs/tags/{version}", timeout=60).read()
        if hashlib.sha256(data).hexdigest() != digest:
            raise RuntimeError(f"{name} archive failed SHA-256 verification")
        archive_path = workspace / f"{name}-{version}.tar.gz"
        archive_path.write_bytes(data)
        with tarfile.open(archive_path) as archive:
            # Extraction is allowed only after matching the exact reviewed archive.
            archive.extractall(workspace)
        source = workspace / f"{name}-{version}"
        build = workspace / f"{name}-build"
        options = [f"-D{name}_BUILD_AUDIO=ON", f"-D{name}_BUILD_NETWORK=OFF",
                   f"-D{name}_BUILD_EXAMPLES=OFF", f"-D{name}_BUILD_DOC=OFF"]
        if name == "CSFML":
            options += ["-DCSFML_LINK_SFML_STATICALLY=OFF", f"-DSFML_ROOT={prefix}"]
        subprocess.run(["cmake", "-S", str(source), "-B", str(build), "-A", "x64",
                        f"-DCMAKE_INSTALL_PREFIX={prefix}", *options], check=True)
        subprocess.run(["cmake", "--build", str(build), "--config", "Release", "--parallel", "2"], check=True)
        subprocess.run(["cmake", "--install", str(build), "--config", "Release"], check=True)
        if name == "SFML":
            for dll in (source / "extlibs/bin/x64").glob("*.dll"):
                shutil.copyfile(dll, prefix / "bin" / dll.name.lower())
            copy_dependency_licenses(source, licenses)
        for license_file in source.glob("license.*"):
            shutil.copyfile(license_file, licenses / f"{name.lower()}-{license_file.name.lower()}")


if __name__ == "__main__":
    main()
