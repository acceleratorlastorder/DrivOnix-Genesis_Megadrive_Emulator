"""Versioned little-endian protocol; workers receive inputs, never expected results."""
from __future__ import annotations

import struct
import subprocess

from corpus import CorpusError

STATE_FIELDS = ["pc", "sr", "stop", *(f"d{i}" for i in range(8)),
                *(f"a{i}" for i in range(8)), "usp", "ssp"]


def request(case, optimized):
    initial = case["initial"]
    ram = initial["ram"]
    addresses = sorted(case["expected_ram"])
    values = [0x31515644, case["index"], int(optimized), initial["pc"], initial["sr"],
              *(initial[f"d{i}"] for i in range(8)), *(initial[f"a{i}"] for i in range(7)),
              initial["usp"], initial["ssp"], len(ram)]
    if "actions" in case:
        actions = case["actions"]
        values[0] = 0x32515644
        values[3:3] = [len(actions), *actions]
    for address, value in sorted(ram.items()):
        values.extend((address, value))
    values.append(len(addresses))
    values.extend(addresses)
    if any(not isinstance(value, int) or not 0 <= value <= 0xFFFFFFFF for value in values):
        raise CorpusError("Vector contains a value outside the wire range")
    return struct.pack(f"<{len(values)}I", *values)


def responses(data, allow_partial=False):
    position = 0
    result = []
    while position < len(data):
        if position + 100 > len(data):
            if allow_partial:
                break
            raise CorpusError("Truncated worker response")
        values = struct.unpack_from("<25I", data, position)
        if values[0] != 0x31525644 or values[-1] > 0x20000:
            raise CorpusError("Invalid worker response magic/count")
        end = position + 100 + values[-1] * 8
        if end > len(data):
            if allow_partial:
                break
            raise CorpusError("Truncated worker RAM response")
        state = dict(zip(STATE_FIELDS, values[3:24]))
        state["cycles"] = values[2]
        state["ram"] = dict(struct.iter_unpack("<II", data[position + 100:end]))
        result.append((values[1], state))
        position = end
    return result


def execute(worker, cases, optimized, timeout=60):
    """A CPU crash is attached to its case; later cases run in a fresh worker."""
    remaining = list(cases)
    if len(remaining) > 256:
        # A broken opcode can crash many vectors. Bound recovery traffic rather
        # than re-sending an entire multi-thousand-case group after every crash.
        for first in range(0, len(remaining), 256):
            yield from execute(worker, remaining[first:first + 256], optimized, timeout)
        return
    encoded = [request(case, optimized) for case in remaining]
    while remaining:
        timed_out = False
        try:
            command = list(worker) if isinstance(worker, (tuple, list)) else [str(worker)]
            run = subprocess.run(command, input=b"".join(encoded),
                                 capture_output=True, timeout=timeout)
            output, errors, code = run.stdout, run.stderr, run.returncode
        except subprocess.TimeoutExpired as error:
            output, errors, code = error.stdout or b"", error.stderr or b"", -999
            timed_out = True
        if code == 2:
            raise CorpusError("Worker adapter rejected input: " + errors.decode(errors="replace")[-2000:])
        decoded = responses(output, allow_partial=code != 0)
        if len(decoded) > len(remaining):
            raise CorpusError("Worker returned extra cases")
        for case, (index, state) in zip(remaining, decoded):
            if index != case["index"]:
                raise CorpusError("Worker reordered case IDs")
            yield case, state, None
        remaining = remaining[len(decoded):]
        encoded = encoded[len(decoded):]
        if code == 0:
            if remaining:
                raise CorpusError("Worker silently omitted cases")
            break
        if not remaining:
            raise CorpusError("Worker failed after all responses: " + errors.decode(errors="replace")[-2000:])
        yield remaining[0], None, {"kind": "timeout" if timed_out else "cpu_crash", "return_code": code,
                                  "diagnostic": errors.decode(errors="replace")[-1000:]}
        remaining = remaining[1:]
        encoded = encoded[1:]


def compare(case, actual):
    expected = case["final"]
    differences = {}
    for field in [*(f"d{i}" for i in range(8)), *(f"a{i}" for i in range(8)), "usp", "ssp", "pc", "sr"]:
        mask = case.get("field_masks", {}).get(field, case["sr_mask"] if field == "sr" else 0xFFFFFFFF)
        if actual[field] & mask != expected[field] & mask:
            differences[field] = {"expected": expected[field], "actual": actual[field], "mask": mask}
    if "stop" in expected and bool(actual["stop"]) != bool(expected["stop"]):
        differences["stop"] = {"expected": expected["stop"], "actual": actual["stop"]}
    if case["length"] is not None and actual["cycles"] != case["length"]:
        differences["cycles"] = {"expected": case["length"], "actual": actual["cycles"]}
    memory = case["expected_ram"]
    for address in set(memory) | set(actual["ram"]):
        expected_byte = memory.get(address, 0)
        mask = case.get("memory_masks", {}).get(address, 255)
        if mask and (address not in actual["ram"] or (expected_byte ^ actual["ram"][address]) & mask):
            differences[f"ram:{address:06x}"] = {"expected": expected_byte, "actual": actual["ram"].get(address), "mask": mask}
    return differences
