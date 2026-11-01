"""Fetch/verify pinned oracle sources before an explicit optional reference build."""
import argparse
import concurrent.futures
import json
from pathlib import Path

from corpus import verified_bytes
from fetch import fetch_one


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache", type=Path, default=Path("build/conformance-data"))
    parser.add_argument("--verify-only", action="store_true")
    args = parser.parse_args()
    metadata = json.loads(Path(__file__).with_name("reference.lock.json").read_text())
    if args.verify_only:
        for entry in metadata["files"]:
            verified_bytes(args.cache, entry)
    else:
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
            list(pool.map(lambda entry: fetch_one(args.cache, entry), metadata["files"]))
    print("Verified independent MC68000 reference:", metadata["source"]["commit"])


if __name__ == "__main__":
    main()
