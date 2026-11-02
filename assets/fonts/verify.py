"""Verify pinned font and license bytes without network access."""
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parent
manifest = json.loads((root / "manifest.json").read_text(encoding="utf-8"))
for entry in manifest["files"]:
    data = (root / entry["path"]).read_bytes()
    if len(data) != entry["size"]:
        raise RuntimeError(f"Font asset size changed: {entry['path']}")
    if hashlib.sha256(data).hexdigest() != entry["sha256"]:
        raise RuntimeError(f"Font asset changed: {entry['path']}")
print("Pinned Unifont assets and upstream notices verified")
