"""Publish tested master/version builds and retry incomplete drafts safely."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
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


def ensure_tag(repo, tag, commit):
    try:
        gh("api", f"repos/{repo}/git/ref/tags/{tag}")
    except subprocess.CalledProcessError as error:
        if "HTTP 404" not in (error.stderr or ""):
            raise
        gh("api", f"repos/{repo}/git/refs", "--method", "POST",
           "-f", "ref=refs/tags/" + tag, "-f", "sha=" + commit)
    # Existing tags are never moved, including if their short SHA collides.
    verify_tag(repo, tag, commit)


def publication_version(event, ref, commit, requested, enabled):
    if event == "push" and ref.startswith("refs/tags/v"):
        return select_version(ref, commit, requested)
    if ref == "refs/heads/master" and event == "push":
        expected = select_version(ref, commit)
        if requested and requested != expected:
            raise ValueError("Automatic master releases must use their source development version")
        return expected
    if event == "workflow_dispatch" and ref == "refs/heads/master" and enabled:
        return select_version(ref, commit, requested)
    raise ValueError("This event/ref is a preview and must not publish")


def complete_existing(record, commit, names):
    assets = record.get("assets", [])
    return (record.get("target_commitish") == commit
            and {a["name"] for a in assets} == set(names)
            and all(a.get("state") == "uploaded" and a.get("size", 0) > 0
                    and re.fullmatch(r"sha256:[0-9a-f]{64}", a.get("digest") or "") for a in assets))


def publish(directory, repo, ref, commit, requested, event="push", enabled=False):
    version = publication_version(event, ref, commit, requested, enabled)
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

    if ref == "refs/heads/master":
        ensure_tag(repo, tag, commit)
    verify_tag(repo, tag, commit)
    existing = release(repo, tag)
    if existing and not existing["draft"]:
        # Rebuilding one commit can change archive timestamps. Retain the first
        # complete published build; rerunning it never replaces public assets.
        if complete_existing(existing, commit, names):
            print(f"Already published https://github.com/{repo}/releases/tag/{tag}")
            return
        raise ValueError("Existing public release does not match this complete source build")
    if existing and existing["target_commitish"] != commit:
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
    publish(args.directory, os.environ["GITHUB_REPOSITORY"], os.environ["GITHUB_REF"],
            os.environ["GITHUB_SHA"], os.environ["PACKAGE_VERSION"],
            os.environ["GITHUB_EVENT_NAME"], os.environ.get("PUBLISH_RELEASE") == "true")


if __name__ == "__main__":
    main()
