"""Build pinned native 64-bit Windows dependencies with the same MSVC toolchain."""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import tarfile
import urllib.request


SOURCES = (
    ("SFML", "2.6.2", "15ff4d608a018f287c6a885db0a2da86ea389e516d2323629e4d4407a7ce047f"),
    ("CSFML", "2.6.1", "f3f3980f6b5cad85b40e3130c10a2ffaaa9e36de5f756afd4aacaed98a7a9b7b"),
)


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
                shutil.copyfile(dll, prefix / "bin" / dll.name)
            dependency_licenses = source / "extlibs/licenses"
            if dependency_licenses.is_dir():
                shutil.copytree(dependency_licenses, licenses / "SFML-dependencies", dirs_exist_ok=True)
        for license_file in source.glob("license.*"):
            shutil.copyfile(license_file, licenses / f"{name}-{license_file.name}")


if __name__ == "__main__":
    main()
