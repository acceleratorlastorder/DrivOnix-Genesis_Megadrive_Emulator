#!/usr/bin/env python3
"""Fetch immutable external test data and verify every Git blob hash."""
import argparse
import concurrent.futures
import hashlib
import os
from pathlib import Path
import time
import urllib.request

from corpus import CorpusError, git_blob_sha, lock, verified_bytes


def fetch_one(cache, entry):
    relative = Path(entry["cache_path"])
    if relative.is_absolute() or ".." in relative.parts:
        raise CorpusError("Unsafe cache path")
    destination = cache / relative
    if destination.exists():
        verified_bytes(cache, entry)
        return
    error = None
    for attempt in range(3):
        try:
            request = urllib.request.Request(entry["url"], headers={"User-Agent": "DrivOnix-conformance"})
            with urllib.request.urlopen(request, timeout=60) as response:
                data = response.read(entry["bytes"] + 1)
            if len(data) != entry["bytes"] or git_blob_sha(data) != entry["git_blob_sha"]:
                raise CorpusError(f"Downloaded hash mismatch: {entry['cache_path']}")
            destination.parent.mkdir(parents=True, exist_ok=True)
            temporary = destination.with_suffix(destination.suffix + ".part")
            with temporary.open("wb") as output:
                output.write(data)
                output.flush()
                os.fsync(output.fileno())
            temporary.replace(destination)
            return
        except Exception as caught:
            error = caught
            if attempt < 2:
                time.sleep(attempt + 1)
    raise CorpusError(f"Fetch failed: {entry['cache_path']}: {error}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache", type=Path, default=Path("build/conformance-data"))
    parser.add_argument("--jobs", type=int, default=6)
    args = parser.parse_args()
    entries = lock()["files"]
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, min(args.jobs, 8))) as pool:
        futures = [pool.submit(fetch_one, args.cache, entry) for entry in entries]
        for number, future in enumerate(concurrent.futures.as_completed(futures), 1):
            future.result()
            if number % 16 == 0 or number == len(entries):
                print(f"Verified {number}/{len(entries)} upstream files", flush=True)
    print(f"Pinned data ready: {args.cache}")


if __name__ == "__main__":
    main()
