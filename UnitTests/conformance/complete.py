#!/usr/bin/env python3
"""Run all readable instruction groups and every 16-bit decode against an oracle."""
import argparse
from collections import Counter, defaultdict
import csv
import gzip
import hashlib
import json
from pathlib import Path
import sys

from corpus import CorpusError, lock, verified_bytes
from protocol import compare, execute
from run import canonical_hash, case_digest, failing_bits, gate, operation
from specifications import instruction_cases, sequence_cases
from sweep import PATTERNS, expected_from_reference, inputs

HERE = Path(__file__).resolve().parent


def load_baseline(path):
    return json.loads(gzip.decompress(path.read_bytes()) if path.suffix == ".gz" else path.read_text())


def save_baseline(path, value):
    raw = (json.dumps(value, separators=(",", ":"), sort_keys=True) + "\n").encode()
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(gzip.compress(raw, mtime=0) if path.suffix == ".gz" else raw)


def run(args):
    cases = instruction_cases()
    required = set(json.loads((HERE / "legacy-groups.json").read_text()))
    covered = {case["group"] for case in cases}
    if covered != required:
        raise CorpusError(f"Instruction specification groups changed: missing={required-covered}, extra={covered-required}")
    catalog_entry = next(entry for entry in lock()["files"] if entry["cache_path"].startswith("map/"))
    catalog = json.loads(verified_bytes(args.cache, catalog_entry)) if not args.specifications_only else {}
    groups, totals, observed = defaultdict(Counter), Counter(), {}
    examples, crash_examples, oracle_disagreements = [], [], []
    failure_categories = Counter()
    expectation_digest = hashlib.sha256()
    encodings, legal_encodings, illegal_encodings = set(), set(), set()
    paths = ("reference", "optimized")
    track = bool(args.baseline or args.record_baseline)
    checked_reference = False

    def evaluate(batch, name):
        for item in batch:
            expectation_digest.update(f"{name}/{item['index']}/{case_digest(item)}\n".encode())
        for dispatch in paths:
            for item, actual, error in execute(args.worker, batch, dispatch == "optimized", args.timeout):
                differences = {error["kind"]: error} if error else compare(item, actual)
                status = "crashed" if error else "failed" if differences else "passed"
                row = groups[item.get("group", name) + "/" + dispatch]
                row["executed"] += 1
                row[status] += 1
                totals["executed"] += 1
                totals[status] += 1
                if not error:
                    row["state_failed" if any(k != "cycles" for k in differences) else "state_passed"] += 1
                    if "cycles" in differences: row["timing_failed"] += 1
                    if item["length"] is None: row["timing_unchecked"] += 1
                if differences:
                    failure_categories.update(field.split(":")[0] for field in differences)
                    key = f"{name}/{item['index']}/{dispatch}"
                    if track: observed[key] = failing_bits(differences)
                    if error and len(crash_examples) < 10:
                        crash_examples.append({"case": item["name"], "dispatch": dispatch, **error})
                    if len(examples) < 30:
                        examples.append({"case": item["name"], "dispatch": dispatch, "differences": differences})

    # Readable expected outputs are never replaced by either core's outputs.
    evaluate(cases, "specifications")
    sequences = sequence_cases()
    evaluate(sequences, "sequences")
    if args.oracle:
        for item, state, error in execute(args.oracle, cases + sequences, False, args.timeout):
            differences = {error["kind"]: error} if error else compare(item, state)
            if differences:
                oracle_disagreements.append({"case": item["name"], "differences": differences})
        checked_reference = True
        known = json.loads((HERE / "reference-limitations.json").read_text())
        if {item["case"]: failing_bits(item["differences"]) for item in oracle_disagreements} != known:
            raise CorpusError("Independent reference disagrees with readable specifications differently; review oracle limitations")
    if not args.specifications_only:
        if not args.oracle:
            raise CorpusError("Decode sweep requires --oracle built from verified pinned sources")
        patterns = range(1 if args.quick else len(PATTERNS))
        # Bound process input, output, and report memory independently of corpus size.
        for pattern in patterns:
            for first in range(0, 65536, args.batch_size):
                source = list(inputs(range(first, min(first + args.batch_size, 65536)), pattern))
                batch = []
                for item, state, error in execute(args.oracle, source, False, args.timeout):
                    if error:
                        raise CorpusError(f"Reference failed on {item['name']}: {error}")
                    opcode = item["opcode"]
                    encodings.add(opcode)
                    legal = catalog[f"{opcode:04x}"] != "None" or opcode == 0x4AFC
                    (legal_encodings if legal else illegal_encodings).add(opcode)
                    expected = expected_from_reference(item, state, catalog)
                    expected["group"] = operation(opcode, catalog)
                    batch.append(expected)
                evaluate(batch, "decode_sweep")
                if first % 16384 == 0:
                    print(f"Pattern {pattern + 1}: reached opcode {first:04x}", flush=True)
            print(f"Completed all 65,536 opcode words, pattern {pattern + 1}/{len(patterns)}", flush=True)
    plan = {"format": 1, "paths": paths, "quick": args.quick, "profile": args.profile,
            "specifications_only": args.specifications_only,
            "source_hashes": {name: canonical_hash((HERE / name).read_text()) for name in
                              ("specifications.py", "sweep.py", "reference.lock.json", "reference-limitations.json", "legacy-groups.json",
                               "referenceAdapter.cpp", "referenceBridge.c", "protocol.py", "corpus.py", "run.py", "complete.py")},
            "expectations_sha256": expectation_digest.hexdigest()}
    # JSON has lists, not tuples; normalize before comparing a persisted plan.
    plan["paths"] = list(paths)
    regressions, resolved = ({}, 0)
    if args.baseline: regressions, resolved = gate(plan, observed, load_baseline(args.baseline))
    if args.record_baseline: save_baseline(args.record_baseline, {"format": 1, "plan": plan, "known_failures": observed})
    report = {"format": 1, "scope": "MC68000 state specifications and independent differential opcode sweep",
              "hardware_complete": False, "plan": plan, "totals": totals,
              "instruction_groups": {"required": len(required), "covered": len(covered), "missing": sorted(required-covered)},
              "decode_coverage": {"all_words": 65536, "executed_words": len(encodings),
                                  "catalogued_legal_plus_ILLEGAL": len(legal_encodings), "other_words": len(illegal_encodings)},
              "oracle_validated_against_specifications": checked_reference,
              "oracle_disagreements": oracle_disagreements, "groups": dict(sorted(groups.items())),
              "known_failure_gate": {"used": bool(args.baseline), "regressions": regressions, "resolved_bits": resolved},
              "examples": examples, "crash_examples": crash_examples, "failing_assertion_categories": failure_categories,
              "not_observed": ["electrical bus pins, wait states and bus arbitration", "external RESET device effects",
                               "final internal prefetch queue", "Z80 CPU; validated separately from this 68000 worker"]}
    args.report_dir.mkdir(parents=True, exist_ok=True)
    (args.report_dir / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    columns = ("group_and_dispatch", "executed", "passed", "failed", "crashed", "state_passed", "state_failed", "timing_failed", "timing_unchecked")
    with (args.report_dir / "instructions.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=columns)
        writer.writeheader()
        for name, counts in sorted(groups.items()):
            writer.writerow({"group_and_dispatch": name, **{key: counts.get(key, 0) for key in columns[1:]}})
    print("TOTAL", json.dumps(totals, sort_keys=True))
    print("Instruction groups:", len(covered), "/", len(required), "; opcode words:", len(encodings), "/ 65536")
    if args.baseline:
        print("Regression gate:", len(regressions), "new failing cases;", resolved, "resolved bits")
        return int(bool(regressions))
    if args.mode == "audit":
        print("AUDIT COMPLETE: known CPU failures remain failures in the report")
        return 0
    return int(bool(totals["failed"] or totals["crashed"]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--worker", type=Path, required=True)
    parser.add_argument("--oracle", type=Path)
    parser.add_argument("--cache", type=Path, default=Path("build/conformance-data"))
    parser.add_argument("--quick", action="store_true", help="One input pattern per opcode instead of eight")
    parser.add_argument("--specifications-only", action="store_true")
    parser.add_argument("--batch-size", type=int, default=256)
    parser.add_argument("--timeout", type=float, default=60)
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--record-baseline", type=Path)
    parser.add_argument("--mode", choices=("strict", "audit"), default="strict")
    parser.add_argument("--profile", choices=("normal", "sanitizers"), default="normal")
    parser.add_argument("--report-dir", type=Path, default=Path("build/complete-cpu-report"))
    args = parser.parse_args()
    if args.batch_size < 1 or args.batch_size > 4096 or args.timeout <= 0:
        parser.error("batch size must be 1..4096 and timeout positive")
    if args.baseline and args.record_baseline: parser.error("baseline checking and recording are separate")
    args.worker = args.worker.resolve()
    if args.oracle: args.oracle = args.oracle.resolve()
    return run(args)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (CorpusError, OSError, ValueError) as error:
        print("Complete CPU suite error:", error, file=sys.stderr)
        raise SystemExit(2)
