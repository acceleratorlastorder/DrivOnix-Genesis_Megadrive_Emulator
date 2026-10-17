"""Checks of the oracle adapter, including deliberate corruptions and CPU crashes."""
import argparse
import copy
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

from corpus import CorpusError, REGISTERS, decode, git_blob_sha, handwritten, indices, normalize, verified_bytes
from protocol import compare, execute, responses
from run import failing_bits, gate, operation


def section(magic, body):
    return struct.pack("<II", len(body) + 8, magic) + body


def fixture():
    initial = dict.fromkeys(REGISTERS, 0)
    initial.update(sr=0x2700, pc=0x1004, usp=0x8000, ssp=0x9000)
    final = {**initial, "d0": 0xFFFFFFFF, "sr": 0x2708, "pc": 0x1006}

    def state(value, prefetch):
        body = struct.pack("<19I", *(value[key] for key in REGISTERS))
        body += struct.pack("<3I", *prefetch, 3)
        body += b"".join(struct.pack("<IH", address, word) for address, word in
                         ((0x1000, 0x70FF), (0x1002, 0x4E71), (0x1004, 0x4E71)))
        return section(0x01234567, body)

    name = b"MOVEQ minus one independent adapter fixture"
    body = section(0x89ABCDEF, struct.pack("<I", len(name)) + name)
    body += state(initial, (0x70FF, 0x4E71)) + state(final, (0x4E71, 0x4E71))
    transaction = struct.pack("<II", 4, 1) + struct.pack("<B6I", 2, 4, 6, 0x1004, 0x4E71, 1, 1)
    body += section(0x456789AB, transaction)
    return struct.pack("<II", 0x1A3F5D71, 1) + section(0xABC12367, body)


def normal_case():
    return normalize(next(decode(fixture())), "MOVE.q")


def correct_actual(case):
    result = {key: value for key, value in case["final"].items() if key not in ("prefetch", "ram")}
    result.update(stop=False, cycles=case["length"], ram=copy.deepcopy(case["expected_ram"]))
    return result


class DecoderTests(unittest.TestCase):
    def test_format_and_pc_translation(self):
        case = normal_case()
        self.assertEqual(case["initial"]["pc"], 0x1000)
        self.assertEqual(case["final"]["pc"], 0x1002)
        self.assertEqual(case["opcode"], 0x70FF)
        self.assertEqual(case["initial"]["ram"][0x1000], 0x70)
        self.assertEqual(case["initial"]["ram"][0x1001], 0xFF)
        self.assertEqual(case["transactions"][0], ["r", 4, 6, 0x1004, ".w", 0x4E71, 1, 1])

    def test_bad_data_is_not_reported_as_cpu_failure(self):
        for bad in (fixture()[:-1], b"bad magic" + fixture()[9:], fixture() + b"extra"):
            with self.subTest(bytes=len(bad)), self.assertRaises(CorpusError):
                list(decode(bad))

    def test_section_lengths_are_checked(self):
        bad = bytearray(fixture())
        struct.pack_into("<I", bad, 8, struct.unpack_from("<I", bad, 8)[0] - 1)
        with self.assertRaises(CorpusError):
            list(decode(bad))

    def test_source_hash_mismatch_is_fatal(self):
        with tempfile.TemporaryDirectory() as where:
            path = Path(where) / "source.bin"
            path.write_bytes(b"changed")
            entry = {"cache_path": "source.bin", "bytes": 7, "git_blob_sha": git_blob_sha(b"correct")}
            with self.assertRaises(CorpusError):
                verified_bytes(where, entry)

    def test_selection_is_independent_of_results(self):
        self.assertEqual(indices(2500, 4), {0, 625, 1250, 1875})
        self.assertEqual(indices(3, 0), {0, 1, 2})

    def test_initial_prefetch_conflict_is_explicit(self):
        test = next(decode(fixture()))
        test["initial"]["prefetch"][0] ^= 1
        self.assertIsNotNone(normalize(test, "MOVE.q")["unsupported"])

    def test_immediate_and_quick_forms_are_distinguished(self):
        catalog = {"0600": "ADD.b #, D0", "5000": "ADD.b 8, D0", "d000": "ADD.b D0, D0"}
        self.assertEqual(operation(0x0600, catalog), "ADDI.b")
        self.assertEqual(operation(0x5000, catalog), "ADDQ.b")
        self.assertEqual(operation(0xD000, catalog), "ADD.b")


class OracleTests(unittest.TestCase):
    def test_correct_state(self):
        case = normal_case()
        self.assertEqual(compare(case, correct_actual(case)), {})

    def test_every_observed_register_can_detect_corruption(self):
        case = normal_case()
        for field in [*(f"d{i}" for i in range(8)), *(f"a{i}" for i in range(8)), "usp", "ssp", "pc", "sr"]:
            actual = correct_actual(case)
            actual[field] ^= 1
            with self.subTest(field=field):
                self.assertIn(field, compare(case, actual))

    def test_memory_and_unexpected_writes(self):
        case = normal_case()
        actual = correct_actual(case)
        actual["ram"][0x1000] ^= 1
        actual["ram"][0x123456] = 99
        failures = compare(case, actual)
        self.assertIn("ram:001000", failures)
        self.assertIn("ram:123456", failures)

    def test_defined_flags_fail_and_undefined_flags_are_masked(self):
        case = normal_case()
        case["sr_mask"] = 0xFFF5
        actual = correct_actual(case)
        actual["sr"] ^= 0x0A
        self.assertEqual(compare(case, actual), {})
        actual["sr"] ^= 1
        self.assertIn("sr", compare(case, actual))

    def test_cycles_and_stop(self):
        case = normal_case()
        case["final"]["stop"] = True
        actual = correct_actual(case)
        actual["cycles"] += 4
        self.assertEqual(set(compare(case, actual)), {"cycles", "stop"})

    def test_regressions_are_not_hidden_by_existing_case_failures(self):
        plan = {"cases": "immutable"}
        baseline = {"plan": plan, "known_failures": {"case": {"d0": 1, "sr": 2}}}
        regressions, resolved = gate(plan, {"case": {"sr": 6, "pc": 1}}, baseline)
        self.assertEqual(regressions, {"case": {"sr": 4, "pc": 1}})
        self.assertEqual(resolved, 1)
        with self.assertRaises(CorpusError):
            gate({"cases": "changed"}, {}, baseline)

    def test_baseline_tracks_individual_wrong_bits(self):
        self.assertEqual(failing_bits({"sr": {"expected": 0x2700, "actual": 0x270A, "mask": 0xFFF5}}), {"sr": 0})
        self.assertEqual(failing_bits({"d0": {"expected": 1, "actual": 9}}), {"d0": 8})


class WorkerTests(unittest.TestCase):
    def test_both_real_dispatch_paths_observe_flat_memory_and_banked_stacks(self):
        for optimized in (False, True):
            case = normal_case()
            result = list(execute(WORKER, [case], optimized))
            self.assertEqual(len(result), 1)
            self.assertIsNone(result[0][2])
            self.assertEqual(compare(case, result[0][1]), {})

    def test_worker_rejects_bad_wire_data(self):
        result = subprocess.run([str(WORKER)], input=b"bad!", capture_output=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn(b"invalid request", result.stderr)
        self.assertEqual(result.stdout, b"")

    def test_crash_does_not_hide_later_cases(self):
        with tempfile.TemporaryDirectory() as where:
            fake = Path(where) / "fake_worker.py"
            fake.write_text('''import os,struct,sys
raw=sys.stdin.buffer.read()
index=struct.unpack_from('<I',raw,4)[0]
if index==0:
    sys.stdout.buffer.write(struct.pack('<25I',0x31525644,0,4,*([0]*22)))
    sys.stdout.buffer.flush()
    os._exit(7)
sys.stdout.buffer.write(struct.pack('<25I',0x31525644,index,4,*([0]*22)))
''')
            cases = [normal_case() for _ in range(3)]
            for index, case in enumerate(cases):
                case["index"] = index
            results = list(execute([sys.executable, str(fake)], cases, False))
            self.assertEqual([case["index"] for case, _, _ in results], [0, 1, 2])
            self.assertIsNone(results[0][2])
            self.assertEqual(results[1][2]["return_code"], 7)
            self.assertIsNone(results[2][2])


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--worker", type=Path, required=True)
    args, rest = parser.parse_known_args()
    WORKER = args.worker.resolve()
    unittest.main(argv=[sys.argv[0], *rest])
