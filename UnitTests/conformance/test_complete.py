"""Verify specification coverage and new worker behavior with independent checks."""
import argparse
import copy
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

from complete import load_baseline, save_baseline
from corpus import CorpusError
from protocol import compare, execute, request
from run import case_digest
from specifications import PC, SSP, DATA, arithmetic, case, condition, instruction_cases, sequence_cases
from sweep import expected_from_reference, inputs
import z80


class Specifications(unittest.TestCase):
    def test_all_original_instruction_groups_have_asserted_cases(self):
        required = set(json.loads(Path(__file__).with_name("legacy-groups.json").read_text()))
        cases = instruction_cases()
        self.assertEqual({c["group"] for c in cases}, required)
        self.assertEqual(len(required), 84)
        self.assertGreater(len(cases), 1500)
        self.assertTrue(all(c["final"] and c["expected_ram"] for c in cases))

    def test_arithmetic_boundaries_have_independent_literal_results(self):
        self.assertEqual(arithmetic(0x7F, 1, 8, 0x2700), (0x80, 0x270A))
        self.assertEqual(arithmetic(0xA55A0000, 1, 16, 0x2700, True), (0xA55AFFFF, 0x2719))
        self.assertEqual(arithmetic(0, 0, 32, 0x2700, extend=True), (0, 0x2700))
        self.assertEqual(arithmetic(0, 0, 32, 0x2704, extend=True), (0, 0x2704))

    def test_condition_complements_cover_every_NZVC_combination(self):
        for sr in range(16):
            for code in range(0, 16, 2):
                self.assertNotEqual(condition(code, sr), condition(code + 1, sr))
        self.assertTrue(condition(12, 0x0A)) # N equals V.
        self.assertFalse(condition(14, 4)) # GT rejects zero.

    def test_all_opcode_words_are_generated_including_illegal(self):
        words = {item["opcode"] for item in inputs(range(65536))}
        self.assertEqual(words, set(range(65536)))

    def test_sequence_actions_and_masks_are_in_expectation_digest(self):
        c = sequence_cases()[0]
        changed = copy.deepcopy(c)
        changed["actions"] = [0]
        self.assertNotEqual(case_digest(c), case_digest(changed))
        changed = copy.deepcopy(c)
        changed["field_masks"] = {"d0": 0}
        self.assertNotEqual(case_digest(c), case_digest(changed))

    def test_reset_does_not_assert_undefined_register_contents(self):
        c = next(c for c in sequence_cases() if c.get("actions") == [9])
        actual = {**c["final"], "ram": c["expected_ram"], "cycles": 0, "stop": False}
        actual["d0"] ^= 0xFFFFFFFF
        self.assertNotIn("d0", compare(c, actual))
        actual["pc"] ^= 2
        self.assertIn("pc", compare(c, actual))

    def test_differential_oracle_never_copies_known_bad_divide_C(self):
        c = next(inputs([0x82C0]))
        actual = {**c["final"], "cycles": 140, "ram": {}, "sr": 0x271F}
        normalized = expected_from_reference(c, actual, {"82c0": "DIVU D0, D1"})
        self.assertEqual(normalized["final"]["sr"] & 1, 0)
        self.assertIsNone(normalized["length"])
        self.assertEqual(normalized["sr_mask"] & 0xC, 0)

    def test_compressed_baselines_round_trip_deterministically(self):
        with tempfile.TemporaryDirectory() as where:
            path = Path(where) / "baseline.json.gz"
            value = {"known_failures": {"case": {"sr": 4}}, "plan": {"vectors": "fixed"}}
            save_baseline(path, value)
            before = path.read_bytes()
            self.assertEqual(load_baseline(path), value)
            save_baseline(path, value)
            self.assertEqual(path.read_bytes(), before)


class WorkerSequences(unittest.TestCase):
    def test_multi_instruction_wire_protocol_runs_both_real_decoders(self):
        c = case("adapter", "two MOVEQ instructions", [0x7001, 0x7202],
                 expected={"d0": 1, "d1": 2, "sr": 0x2710}, actions=[0, 0], cycles=8)
        for optimized in (False, True):
            result = list(execute(WORKER, [c], optimized))
            self.assertIsNone(result[0][2])
            self.assertEqual(compare(c, result[0][1]), {})

    def test_invalid_action_is_an_adapter_error(self):
        c = sequence_cases()[0]
        c["actions"] = [10]
        result = subprocess.run([str(WORKER)], input=request(c, False), capture_output=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn(b"invalid CPU action", result.stderr)


def z80_fixture():
    state = dict.fromkeys(z80.REGISTERS, 0)
    state.update(pc=0x1000, ram=[[0x1000, 0]])
    return {"name": "NOP independent fixture", "initial": state,
            "final": {**state, "pc": 0x1001, "r": 1},
            "cycles": [[0x1000, None, "----"], [0x1000, 0, "r-m-"],
                       [0, None, "----"], [0, None, "----"]]}


class Z80Adapter(unittest.TestCase):
    def test_full_prefix_inventory_is_pinned(self):
        metadata = z80.metadata()
        self.assertEqual(len(metadata["files"]), 1604)
        names = {Path(e["cache_path"]).stem for e in metadata["files"]}
        for stem in ("00", "cb 00", "ed 44", "dd 21", "fd 21", "dd cb __ 00", "fd cb __ ff"):
            self.assertIn(stem, names)

    def test_worker_receives_inputs_never_expected_register_values(self):
        test = z80_fixture()
        payload = z80.request(test, 7)
        self.assertNotIn("final", payload)
        self.assertNotIn("cycles", payload)
        self.assertEqual(payload["initial"]["pc"], 0x1000)

    def test_every_z80_register_and_memory_corruption_is_detected(self):
        test = z80.validate(z80_fixture())
        actual = {"final": copy.deepcopy(test["final"]), "cycles": 4}
        self.assertEqual(z80.compare(test, actual), {})
        for field in z80.REGISTERS:
            changed = copy.deepcopy(actual)
            changed["final"][field] ^= 1
            self.assertIn(field, z80.compare(test, changed))
        actual["final"]["ram"].append([0x9999, 0xFF])
        self.assertIn("ram:9999", z80.compare(test, actual))

    def test_bus_disconnected_values_are_ignored_but_pins_and_ports_are_checked(self):
        test = z80_fixture()
        actual = {"final": test["final"], "cycles": 4, "trace": [[a, 255 if b is None else b, c] for a,b,c in test["cycles"]]}
        self.assertEqual(z80.compare(test, actual, True), {})
        actual["trace"][0][2] = "r-m-"
        actual["ports"] = [[0x100, 0x55, "w"]]
        self.assertEqual(set(z80.compare(test, actual, True)), {"ports", "trace:0"})

    def test_malformed_source_is_an_infrastructure_error(self):
        test = z80_fixture()
        test["initial"]["pc"] = 65536
        with self.assertRaises(CorpusError): z80.validate(test)

    def test_internal_Q_is_a_saved_flag_byte(self):
        test = z80_fixture()
        test["initial"]["q"] = 0xD7
        self.assertIs(z80.validate(test), test)

    def test_real_process_contract_and_result_corruption(self):
        with tempfile.TemporaryDirectory() as where:
            worker = Path(where) / "z80_worker.py"
            worker.write_text('''import json,sys
for line in sys.stdin:
    request=json.loads(line)
    state=request['initial']
    state['pc']=(state['pc']+1)&65535
    state['r']=(state['r']&128)|((state['r']+1)&127)
    print(json.dumps({'index':request['index'],'final':state,'cycles':4}),flush=True)
''')
            test = z80_fixture()
            result = list(z80.execute([sys.executable, str(worker)], [test]))
            self.assertEqual(result[0][1], {})
            test["final"]["pc"] += 1
            self.assertIn("pc", list(z80.execute([sys.executable, str(worker)], [test]))[0][1])

    def test_missing_z80_core_can_never_be_a_strict_pass(self):
        with tempfile.TemporaryDirectory() as where:
            result = subprocess.run([sys.executable, str(Path(z80.__file__)), "--inventory-only", "--report-dir", where], capture_output=True)
            self.assertEqual(result.returncode, 1)
            report = json.loads((Path(where) / "report.json").read_text())
            self.assertIn("blocked", report["status"])
            self.assertEqual(report["totals"].get("passed", 0), 0)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--worker", type=Path, required=True)
    args, remaining = parser.parse_known_args()
    WORKER = args.worker.resolve()
    unittest.main(argv=[sys.argv[0], *remaining])
