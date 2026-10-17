#!/usr/bin/env python3
"""Run readable project specifications and pinned external MC68000 vectors.

Strict mode fails on mismatches. Audit mode writes a diagnostic report, never a
conformance certification. A checked-in baseline permits only already-known
failing assertions for an immutable set of cases; expectations remain correct.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import csv
import hashlib
import json
from pathlib import Path
import struct
import sys

from corpus import CorpusError, LOCK_PATH, decode, handwritten, indices, lock, normalize, verified_bytes
from protocol import compare, execute

HERE = Path(__file__).resolve().parent


def canonical_hash(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def operation(opcode, catalog):
    decoded = catalog.get(f"{opcode:04x}", "None")
    if decoded == "None":
        return "ILLEGAL_LINEA" if opcode >> 12 == 10 else "ILLEGAL_LINEF" if opcode >> 12 == 15 else "ILLEGAL"
    name = decoded.split()[0]
    if "toCCR" not in name and "toSR" not in name:
        immediate = {0x0000: "ORI", 0x0200: "ANDI", 0x0400: "SUBI", 0x0600: "ADDI",
                     0x0A00: "EORI", 0x0C00: "CMPI"}.get(opcode & 0xFF00)
        if immediate and opcode & 0x00C0 != 0x00C0:
            return immediate + "." + "bwl"[(opcode >> 6) & 3]
    if opcode & 0xF000 == 0x5000 and opcode & 0x00C0 != 0x00C0:
        return ("SUBQ" if opcode & 0x100 else "ADDQ") + "." + "bwl"[(opcode >> 6) & 3]
    if opcode & 0xFF00 == 0x6000:
        return "BRA"
    return name


def case_digest(case):
    content = {key: case[key] for key in ("initial", "final", "length", "sr_mask")}
    content.update({key: case[key] for key in ("actions", "field_masks", "memory_masks") if key in case})
    return canonical_hash(content)


def gate(plan, observed, baseline):
    if baseline["plan"] != plan:
        raise CorpusError("Baseline describes different vectors, expectations, or selection; review the new plan")
    allowed = baseline["known_failures"]
    regressions = {}
    for key, fields in observed.items():
        new = {field: mask & ~allowed.get(key, {}).get(field, 0) for field, mask in fields.items()
               if mask & ~allowed.get(key, {}).get(field, 0)}
        if new:
            regressions[key] = new
    resolved = sum(bin(mask & ~observed.get(key, {}).get(field, 0)).count("1")
                   for key, fields in allowed.items() for field, mask in fields.items())
    return regressions, resolved


def failing_bits(differences):
    """Known wrong bits do not license new wrong bits in the same register."""
    result = {}
    for field, detail in differences.items():
        if isinstance(detail.get("expected"), int) and isinstance(detail.get("actual"), int):
            result[field] = (detail["expected"] ^ detail["actual"]) & detail.get("mask", 0xFFFFFFFF)
        else:
            result[field] = 1
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--worker", type=Path, required=True)
    parser.add_argument("--cache", type=Path, default=Path("build/conformance-data"))
    parser.add_argument("--handwritten", type=Path, default=HERE / "handwritten.json")
    parser.add_argument("--dispatch", choices=("both", "reference", "optimized"), default="both")
    parser.add_argument("--limit-per-group", type=int, default=0, help="0 = every case; positive = deterministic spread")
    parser.add_argument("--group", action="append", help="Run only these external groups, plus handwritten cases")
    parser.add_argument("--handwritten-only", action="store_true")
    parser.add_argument("--mode", choices=("strict", "audit"), default="strict")
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--record-baseline", type=Path)
    parser.add_argument("--report-dir", type=Path, default=Path("build/conformance-report"))
    parser.add_argument("--timeout", type=float, default=60)
    args = parser.parse_args(argv)
    if args.limit_per_group < 0 or args.timeout <= 0:
        parser.error("limits must be nonnegative and timeout positive")
    if args.record_baseline and args.baseline:
        parser.error("recording and checking a baseline are separate operations")
    if not args.worker.is_file():
        raise CorpusError(f"Worker does not exist: {args.worker}")
    args.worker = args.worker.resolve()
    metadata = lock()
    if args.handwritten_only:
        catalog = {}
    else:
        map_entry = next(entry for entry in metadata["files"] if entry["cache_path"].startswith("map/"))
        catalog = json.loads(verified_bytes(args.cache, map_entry))
    paths = ("reference", "optimized") if args.dispatch == "both" else (args.dispatch,)
    totals = Counter()
    by_group = []
    by_operation = defaultdict(Counter)
    encodings = defaultdict(Counter)
    failure_fields = Counter()
    observed = {}
    digest = hashlib.sha256()
    selected = 0
    detailed = bool(args.record_baseline or args.baseline)

    def process_group(group, cases, available, quarantine=None):
        nonlocal selected
        rows = {path: Counter(available=available, selected=len(cases)) for path in paths}
        examples = []
        reference_fingerprints = {}
        for case in cases:
            digest.update(f"{group}:{case['index']}:{case_digest(case)}\n".encode())
        selected += len(cases)
        for path in paths:
            counts = rows[path]
            valid = []
            for case in cases:
                status = "quarantined" if quarantine else "unsupported" if case["unsupported"] else None
                if status:
                    counts[status] += 1
                    encodings[case["opcode"]][status] += 1
                    if len(examples) < 3:
                        examples.append({"case": case["name"], "dispatch": path, "status": status,
                                         "reason": quarantine or case["unsupported"]})
                    if detailed:
                        observed[f"{group}/{case['index']}/{path}"] = {status: 1}
                else:
                    valid.append(case)
            for case, actual, error in execute(args.worker, valid, path == "optimized", args.timeout):
                differences = {error["kind"]: error} if error else compare(case, actual)
                if not error:
                    counts["state_failed" if any(field != "cycles" for field in differences) else "state_passed"] += 1
                    if "cycles" in differences:
                        counts["timing_failed"] += 1
                    elif case["length"] is None:
                        counts["timing_unchecked"] += 1
                fingerprint = hashlib.sha256(json.dumps(actual if not error else error["kind"], sort_keys=True).encode()).hexdigest()
                if path == "reference":
                    reference_fingerprints[case["index"]] = fingerprint
                elif case["index"] in reference_fingerprints and reference_fingerprints[case["index"]] != fingerprint:
                    differences["dispatch_divergence"] = {"reference": reference_fingerprints[case["index"]],
                                                          "optimized": fingerprint}
                    counts["dispatch_divergences"] += 1
                status = "crashed" if error else "failed" if differences else "passed"
                counts["executed"] += 1
                counts[status] += 1
                encodings[case["opcode"]]["executed"] += 1
                encodings[case["opcode"]][status] += 1
                family = case.get("family") or operation(case["opcode"], catalog)
                by_operation[family][status] += 1
                by_operation[family]["executed"] += 1
                for field in differences:
                    failure_fields[field.split(":")[0]] += 1
                if differences:
                    if detailed:
                        observed[f"{group}/{case['index']}/{path}"] = failing_bits(differences)
                    if len(examples) < 5:
                        examples.append({"case": case["name"], "index": case["index"], "opcode": f"{case['opcode']:04x}",
                                         "dispatch": path, "status": status, "differences": differences})
            totals.update(counts)
        by_group.append({"group": group, "dispatch": rows, "examples": examples})
        print(f"{group}: " + " | ".join(f"{path}: " + ", ".join(f"{k}={v}" for k, v in rows[path].items())
                                        for path in paths), flush=True)

    project_cases = list(handwritten(args.handwritten))
    process_group("handwritten", project_cases, len(project_cases))
    entries = [entry for entry in metadata["files"] if entry["cache_path"].startswith("v1/")]
    requested = set(args.group or [])
    known_groups = {Path(entry["cache_path"]).name.removesuffix(".json.bin") for entry in entries}
    if requested - known_groups:
        raise CorpusError("Unknown group(s): " + ", ".join(sorted(requested - known_groups)))
    if not args.handwritten_only:
        for entry in entries:
            group = Path(entry["cache_path"]).name.removesuffix(".json.bin")
            if requested and group not in requested:
                continue
            data = verified_bytes(args.cache, entry)
            count = struct.unpack_from("<II", data)[1]
            chosen = indices(count, args.limit_per_group)
            cases = []
            for test in decode(data):
                if test["index"] in chosen:
                    cases.append(normalize(test, group))
            process_group(group, cases, count, metadata["quarantined"].get(group))

    plan = {"adapter_version": 1, "assertion_granularity": "bits", "lock_sha256": canonical_hash(metadata),
            "handwritten_sha256": canonical_hash(json.loads(args.handwritten.read_text())),
            "dispatch": list(paths), "limit_per_group": args.limit_per_group,
            "groups": sorted(requested), "handwritten_only": args.handwritten_only,
            "selected_cases": selected, "expectations_sha256": digest.hexdigest()}
    regressions, resolved = {}, 0
    if args.baseline:
        regressions, resolved = gate(plan, observed, json.loads(args.baseline.read_text()))
    if args.record_baseline:
        args.record_baseline.parent.mkdir(parents=True, exist_ok=True)
        args.record_baseline.write_text(json.dumps({"format": 1, "plan": plan, "known_failures": observed}, indent=2) + "\n")

    matrix = defaultdict(lambda: {"catalogued_encodings": 0, "selected_encodings": 0, "executed_encodings": 0,
                                   "encodings_without_observed_failures": 0})
    for code, description in catalog.items():
        if description == "None":
            continue
        opcode = int(code, 16)
        family = operation(opcode, catalog)
        row = matrix[family]
        row["catalogued_encodings"] += 1
        counts = encodings.get(opcode, {})
        row["selected_encodings"] += bool(counts)
        row["executed_encodings"] += bool(counts.get("executed"))
        row["encodings_without_observed_failures"] += bool(counts.get("executed")) and not any(
            counts.get(key) for key in ("failed", "crashed", "quarantined", "unsupported"))
    for family, counts in by_operation.items():
        matrix[family].update(counts)
    report = {"format": 1, "scope": "Observed MC68000 instruction state and reported cycle totals; not full console validation",
              "not_observed": ["prefetch queue after execution", "bus transaction ordering/function codes/UDS/LDS", "external RESET devices"],
              "complete_hardware_conformance": False, "plan": plan,
              "sources": {"execution": metadata["execution_source"], "catalog": metadata["catalog_source"]},
              "totals": totals, "known_failure_gate": {"used": bool(args.baseline), "regressions": regressions,
                                                         "resolved_assertions": resolved},
              "failing_assertion_categories": failure_fields, "groups": by_group,
              "instruction_matrix": dict(sorted(matrix.items()))}
    args.report_dir.mkdir(parents=True, exist_ok=True)
    (args.report_dir / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    columns = ["instruction", "catalogued_encodings", "selected_encodings", "executed_encodings",
               "encodings_without_observed_failures", "executed", "passed", "failed", "crashed"]
    with (args.report_dir / "instructions.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=columns)
        writer.writeheader()
        for family, row in sorted(matrix.items()):
            writer.writerow({"instruction": family, **{key: row.get(key, 0) for key in columns[1:]}})
    print("TOTAL " + json.dumps(totals, sort_keys=True))
    print(f"Report: {args.report_dir / 'report.json'}")
    if args.baseline:
        print(f"Regression gate: {len(regressions)} new failing cases; {resolved} previously failing assertions resolved")
        return 1 if regressions else 0
    if args.mode == "audit":
        print("AUDIT COMPLETE: this exit status is not a CPU conformance pass")
        return 0
    return int(any(totals[key] for key in ("failed", "crashed", "unsupported", "quarantined")))


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (CorpusError, OSError, ValueError) as error:
        print(f"Conformance runner error: {error}", file=sys.stderr)
        raise SystemExit(2)
