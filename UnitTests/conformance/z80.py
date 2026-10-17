#!/usr/bin/env python3
"""Pinned Z80 specifications and a worker contract; DrivOnix has no Z80 core yet."""
import argparse
from collections import Counter
import concurrent.futures
import json
from pathlib import Path
import subprocess
import sys

from corpus import CorpusError, verified_bytes
from fetch import fetch_one
from run import canonical_hash

HERE = Path(__file__).resolve().parent
BYTE_REGS = ("a", "f", "b", "c", "d", "e", "h", "l", "i", "r", "q")
WORD_REGS = ("pc", "sp", "ix", "iy", "af_", "bc_", "de_", "hl_", "wz")
BITS = ("ei", "p", "iff1", "iff2")
REGISTERS = (*BYTE_REGS, *WORD_REGS, *BITS, "im")


def metadata():
    return json.loads((HERE / "z80.lock.json").read_text())


def validate(test):
    if not isinstance(test.get("name"), str): raise CorpusError("Z80 case lacks a name")
    for side in ("initial", "final"):
        state = test[side]
        for field in REGISTERS:
            value = state[field]
            upper = 255 if field in BYTE_REGS else 65535 if field in WORD_REGS else 2 if field == "im" else 1
            if type(value) is not int or not 0 <= value <= upper:
                raise CorpusError(f"Invalid Z80 {side}.{field}: {value}")
        addresses = set()
        for address, value in state["ram"]:
            if not 0 <= address <= 65535 or not 0 <= value <= 255 or address in addresses:
                raise CorpusError("Invalid or duplicate Z80 memory entry")
            addresses.add(address)
    for address, value, pins in test["cycles"]:
        if address is not None and not 0 <= address <= 65535: raise CorpusError("Invalid Z80 bus address")
        if value is not None and not 0 <= value <= 255: raise CorpusError("Invalid Z80 bus data")
        if len(pins) != 4 or any(c not in "rwmi-" for c in pins): raise CorpusError("Invalid Z80 bus pins")
    for address, value, kind in test.get("ports", []):
        if not 0 <= address <= 65535 or not 0 <= value <= 255 or kind not in ("r", "w"):
            raise CorpusError("Invalid Z80 port transaction")
    return test


def request(test, index):
    observed = sorted(dict(test["initial"]["ram"]) | dict(test["final"]["ram"]))
    return {"format": "drivonix-z80-v1", "index": index, "initial": test["initial"],
            "input_ports": [port for port in test.get("ports", []) if port[2] == "r"],
            "observe_memory": observed}


def compare(test, actual, pins=False):
    differences = {}
    for field in REGISTERS:
        expected = test["final"][field]
        if actual["final"].get(field) != expected:
            differences[field] = {"expected": expected, "actual": actual["final"].get(field)}
    expected_ram = {**dict(test["initial"]["ram"]), **dict(test["final"]["ram"])}
    actual_ram = dict(actual["final"]["ram"])
    for address in expected_ram.keys() | actual_ram.keys():
        if actual_ram.get(address) != expected_ram.get(address, 0):
            differences[f"ram:{address:04x}"] = {"expected": expected_ram.get(address, 0), "actual": actual_ram.get(address)}
    if actual.get("cycles") != len(test["cycles"]):
        differences["cycles"] = {"expected": len(test["cycles"]), "actual": actual.get("cycles")}
    if actual.get("ports", []) != test.get("ports", []):
        differences["ports"] = {"expected": test.get("ports", []), "actual": actual.get("ports", [])}
    if pins:
        trace = actual.get("trace", [])
        if len(trace) != len(test["cycles"]):
            differences["trace_length"] = {"expected": len(test["cycles"]), "actual": len(trace)}
        else:
            for index, (expected, observed) in enumerate(zip(test["cycles"], trace)):
                # null address/data represent disconnected pins, not a required zero.
                if len(observed) != 3 or any(value is not None and value != observed[i] for i, value in enumerate(expected)):
                    differences[f"trace:{index}"] = {"expected": expected, "actual": observed}
    return differences


def execute(worker, tests, pins=False, timeout=60):
    pending = list(enumerate(tests))
    while pending:
        raw = "".join(json.dumps(request(test, index), separators=(",", ":")) + "\n" for index, test in pending)
        try:
            command = list(worker) if isinstance(worker, (list, tuple)) else [str(worker)]
            result = subprocess.run(command, input=raw, text=True, capture_output=True, timeout=timeout)
        except subprocess.TimeoutExpired as error:
            raise CorpusError("Z80 worker timed out") from error
        replies = [json.loads(line) for line in result.stdout.splitlines() if line.strip()]
        if len(replies) > len(pending): raise CorpusError("Z80 worker returned extra cases")
        for (index, test), reply in zip(pending, replies):
            if reply.get("index") != index: raise CorpusError("Z80 worker reordered cases")
            yield test, compare(test, reply, pins)
        pending = pending[len(replies):]
        if result.returncode == 0:
            if pending: raise CorpusError("Z80 worker silently omitted cases")
            break
        if not pending: raise CorpusError("Z80 worker failed after its last response")
        yield pending[0][1], {"cpu_crash": {"return_code": result.returncode, "diagnostic": result.stderr[-1000:]}}
        pending = pending[1:]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache", type=Path, default=Path("build/conformance-data"))
    parser.add_argument("--fetch", action="store_true")
    parser.add_argument("--inventory-only", action="store_true")
    parser.add_argument("--group", action="append", help="Exact filename stem, for example 'cb 00'")
    parser.add_argument("--worker", type=Path)
    parser.add_argument("--pins", action="store_true")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--mode", choices=("strict", "audit"), default="strict")
    parser.add_argument("--report-dir", type=Path, default=Path("build/z80-report"))
    args = parser.parse_args()
    source = metadata()
    entries = source["files"]
    requested = set(args.group or [])
    if requested:
        known = {Path(entry["cache_path"]).stem for entry in entries}
        if requested - known: raise CorpusError("Unknown Z80 groups: " + str(requested - known))
        entries = [entry for entry in entries if Path(entry["cache_path"]).stem in requested]
    if args.fetch:
        with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, min(args.jobs, 8))) as pool:
            futures = [pool.submit(fetch_one, args.cache, entry) for entry in entries]
            for number, future in enumerate(concurrent.futures.as_completed(futures), 1):
                future.result()
                if number % 128 == 0 or number == len(entries): print(f"Verified Z80 files: {number}/{len(entries)}", flush=True)
    totals, groups, examples = Counter(), {}, []
    for entry in entries:
        group = Path(entry["cache_path"]).stem
        row = Counter(available=source["source"]["cases_per_group"])
        if args.inventory_only:
            row["not_loaded"] = row["available"]
        else:
            tests = json.loads(verified_bytes(args.cache, entry))
            if len(tests) != row["available"]: raise CorpusError("Unexpected Z80 case count in " + group)
            for test in tests: validate(test)
            row["validated_specifications"] = len(tests)
            if args.worker:
                for test, differences in execute(args.worker.resolve(), tests, args.pins):
                    row["executed"] += 1
                    row["failed" if differences else "passed"] += 1
                    if differences and len(examples) < 10: examples.append({"case": test["name"], "differences": differences})
            else:
                row["blocked_missing_core"] = len(tests)
        groups[group] = row
        totals.update(row)
    report = {"format": 1, "source": source["source"], "lock_sha256": canonical_hash(source),
              "instruction_groups": len(entries), "totals": totals, "groups": groups, "examples": examples,
              "cpu_coverage_complete": False,
              "backend": str(args.worker) if args.worker else None,
              "status": "executed" if args.worker else "blocked: DrivOnix has no Z80 core"}
    args.report_dir.mkdir(parents=True, exist_ok=True)
    (args.report_dir / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(report["status"], "; groups:", len(entries), ";", dict(totals))
    if args.mode == "audit":
        print("AUDIT COMPLETE: validating specifications is not executing or passing Z80 CPU tests")
        return 0
    return int(not args.worker or bool(totals["failed"]) or args.inventory_only)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (CorpusError, OSError, ValueError, KeyError, TypeError) as error:
        print("Z80 suite error:", error, file=sys.stderr)
        raise SystemExit(2)
