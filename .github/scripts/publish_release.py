"""Publish only complete, tested tag builds; retry incomplete drafts safely."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

from release_version import select_version


def gh(*args):
    return subprocess.run(["gh", *args], check=True, capture_output=True, text=True).stdout


def release(repo, tag):
    # The by-tag REST endpoint does not find pending draft tags. The
    # authenticated release listing includes drafts and supports safe retries.
    pages = json.loads(gh("api", f"repos/{repo}/releases?per_page=100", "--paginate", "--slurp"))
    return next((item for page in pages for item in page if item["tag_name"] == tag), None)


def verify_tag(repo, tag, commit):
    obj = json.loads(gh("api", f"repos/{repo}/git/ref/tags/{tag}"))["object"]
    while obj["type"] == "tag":
        obj = json.loads(gh("api", f"repos/{repo}/git/tags/{obj['sha']}"))["object"]
    if obj["type"] != "commit" or obj["sha"] != commit:
        raise ValueError("Release tag moved away from the tested source commit")


def publish(directory, repo, ref, commit, requested):
    if not ref.startswith("refs/tags/v"):
        raise ValueError("Only a pushed version tag may publish a release")
    version = select_version(ref, commit, requested)
    tag = "v" + version
    names = [f"DrivOnix-{version}-linux-x86_64.tar.gz",
             f"DrivOnix-{version}-linux-x86_64.deb",
             f"DrivOnix-{version}-windows-x86_64.zip"]
    found = {p.name for p in directory.iterdir() if p.is_file()}
    if found - {"SHA256SUMS"} != set(names):
        raise ValueError("A release requires exactly the verified Linux tarball/deb and Windows zip")
    hashes = {name: hashlib.sha256((directory / name).read_bytes()).hexdigest() for name in names}
    (directory / "SHA256SUMS").write_text("".join(f"{hashes[name]}  {name}\n" for name in sorted(names)), encoding="utf-8")
    names.append("SHA256SUMS")
    hashes["SHA256SUMS"] = hashlib.sha256((directory / "SHA256SUMS").read_bytes()).hexdigest()

    verify_tag(repo, tag, commit)
    existing = release(repo, tag)
    if existing and (not existing["draft"] or existing["target_commitish"] != commit):
        raise ValueError("Refusing to overwrite a published release or another source's draft")
    if not existing:
        notes = ("Windows x64: extract the ZIP and run bin/DrivOnix.exe.\n\n"
                 "Ubuntu 24.04 x64: install the .deb with sudo apt install ./DrivOnix-*.deb. "
                 "The Linux tarball requires CSFML 2 and GTK 3 runtime libraries.\n\n"
                 "SHA256SUMS covers all three packages. Source commit: " + commit)
        args = ["release", "create", tag, "--repo", repo, "--verify-tag", "--target", commit,
                "--draft", "--generate-notes", "--title", "DrivOnix " + tag, "--notes", notes]
        if "-" in version:
            args.append("--prerelease")
        gh(*args)
    # Replacement is limited to an unpublished draft belonging to this commit.
    gh("release", "upload", tag, *[str(directory / name) for name in names], "--repo", repo, "--clobber")
    uploaded = release(repo, tag)
    assets = {a["name"]: a for a in uploaded["assets"]}
    if set(assets) != set(names):
        raise ValueError("Draft release assets are incomplete or contain unexpected files")
    for name, asset in assets.items():
        if asset["size"] != (directory / name).stat().st_size or asset["state"] != "uploaded":
            raise ValueError("Incomplete release asset: " + name)
        if asset.get("digest") and asset["digest"] != "sha256:" + hashes[name]:
            raise ValueError("Release asset checksum mismatch: " + name)
    # No public release exists until every package and the checksum file arrived.
    verify_tag(repo, tag, commit)
    gh("release", "edit", tag, "--repo", repo, "--draft=false",
       "--prerelease=" + str("-" in version).lower(), "--latest=" + str("-" not in version).lower())
    print(f"Published https://github.com/{repo}/releases/tag/{tag}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, required=True)
    args = parser.parse_args()
    if os.environ.get("GITHUB_EVENT_NAME") != "push":
        raise ValueError("Preview builds must not publish")
    publish(args.directory, os.environ["GITHUB_REPOSITORY"], os.environ["GITHUB_REF"],
            os.environ["GITHUB_SHA"], os.environ["PACKAGE_VERSION"])


if __name__ == "__main__":
    main()
