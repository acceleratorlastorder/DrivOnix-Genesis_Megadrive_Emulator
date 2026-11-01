"""Select a validated version for tag releases and unpublished CI packages."""
import os
from pathlib import Path
import re


VERSION = re.compile(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-([0-9a-z-]+(?:\.[0-9a-z-]+)*))?")


def validate_version(version):
    match = VERSION.fullmatch(version)
    if not match or any(p.isdigit() and len(p) > 1 and p[0] == "0"
                        for p in (match[4] or "").split(".")):
        raise ValueError("Use a version such as 1.2.3 or 1.2.3-rc.1 (without a leading v)")
    return version


def select_version(ref, commit, requested=""):
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("A full source commit SHA is required")
    if ref.startswith("refs/tags/"):
        tag = ref.removeprefix("refs/tags/")
        if not tag.startswith("v"):
            raise ValueError("Release tags must start with v")
        version = validate_version(tag[1:])
        if requested and requested != version:
            raise ValueError("Package version must match the release tag")
        return version
    return validate_version(requested) if requested else "0.0.0-dev.g" + commit[:12]


def main():
    version = select_version(os.environ["GITHUB_REF"], os.environ["GITHUB_SHA"],
                             os.environ.get("PACKAGE_VERSION", ""))
    if os.environ.get("GITHUB_OUTPUT"):
        with Path(os.environ["GITHUB_OUTPUT"]).open("a", encoding="utf-8") as out:
            out.write(f"version={version}\n")
    print(version)


if __name__ == "__main__":
    main()
