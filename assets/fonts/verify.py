"""Verify pinned font and license bytes without network access."""
import hashlib
import json
from pathlib import Path
from fontcmap import coverage, ranges

root = Path(__file__).resolve().parent
manifest = json.loads((root / "manifest.json").read_text(encoding="utf-8"))
for entry in manifest["files"]:
    data = (root / entry["path"]).read_bytes()
    if len(data) != entry["size"]:
        raise RuntimeError(f"Font asset size changed: {entry['path']}")
    if hashlib.sha256(data).hexdigest() != entry["sha256"]:
        raise RuntimeError(f"Font asset changed: {entry['path']}")
regular = (root / "roboto-regular.ttf").read_bytes()
medium = (root / "roboto-medium.ttf").read_bytes()
locked = json.loads((root / "roboto-coverage.json").read_text(encoding="utf-8"))
if locked["schema"] != 1 or locked["fonts"] != {
    "regular_sha256": hashlib.sha256(regular).hexdigest(),
    "medium_sha256": hashlib.sha256(medium).hexdigest(),
}:
    raise RuntimeError("Roboto coverage refers to different font bytes")
if locked["ranges"] != ranges(coverage(regular) & coverage(medium)):
    raise RuntimeError("Roboto fallback coverage does not match both pinned font cmaps")
print("Pinned Unifont/Roboto fonts, coverage and upstream notices verified")
