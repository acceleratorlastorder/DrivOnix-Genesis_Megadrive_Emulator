#!/usr/bin/env python3
"""Verify the exact vendored CPU/audio/parser bytes without network access."""
import hashlib
import json
from pathlib import Path


def main():
    root = Path(__file__).resolve().parent.parent
    lock = json.loads((root / "ThirdParty/sound-z80.lock.json").read_text())
    for entry in lock["files"]:
        path = root / entry["path"]
        actual = hashlib.sha256(path.read_bytes()).hexdigest()
        if actual != entry["sha256"]:
            raise RuntimeError(f"Vendored source changed: {entry['path']}")
    print(f"Verified {len(lock['files'])} pinned Z80/audio/parser source and license files")


if __name__ == "__main__":
    main()
