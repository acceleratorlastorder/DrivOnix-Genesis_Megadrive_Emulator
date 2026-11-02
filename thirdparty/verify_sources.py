#!/usr/bin/env python3
"""Verify the exact vendored CPU/audio/parser bytes without network access."""
import hashlib
import json
from pathlib import Path


def main():
    root = Path(__file__).resolve().parent.parent
    entries = []
    for name in ("sound-z80.lock.json", "sqlite.lock.json"):
        entries.extend(json.loads((root / "thirdparty" / name).read_text())["files"])
    for entry in entries:
        path = root / entry["path"]
        actual = hashlib.sha256(path.read_bytes()).hexdigest()
        if actual != entry["sha256"]:
            raise RuntimeError(f"Vendored source changed: {entry['path']}")
    print(f"Verified {len(entries)} pinned CPU/audio/parser/SQLite source and license files")


if __name__ == "__main__":
    main()
