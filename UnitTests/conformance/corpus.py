"""Pinned MAME single-step data, decoded without running upstream code.

Binary format: SingleStepTests/m68000 decode.py (MIT; see upstream-license.txt).
"""
from __future__ import annotations

import hashlib
import json
import struct
from pathlib import Path

REGISTERS = [*(f"d{i}" for i in range(8)), *(f"a{i}" for i in range(7)), "usp", "ssp", "sr", "pc"]
LOCK_PATH = Path(__file__).with_name("upstream.lock.json")


class CorpusError(ValueError):
    pass


def lock():
    return json.loads(LOCK_PATH.read_text())


def git_blob_sha(data):
    return hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()


def verified_bytes(cache, entry):
    path = Path(cache) / entry["cache_path"]
    try:
        data = path.read_bytes()
    except FileNotFoundError as error:
        raise CorpusError(f"Missing {path}; run fetch.py first") from error
    if len(data) != entry["bytes"] or git_blob_sha(data) != entry["git_blob_sha"]:
        raise CorpusError(f"Integrity check failed: {path}")
    return data


class Reader:
    def __init__(self, data):
        self.data = data
        self.position = 0

    def take(self, size):
        end = self.position + size
        if size < 0 or end > len(self.data):
            raise CorpusError("Truncated binary corpus")
        value = self.data[self.position:end]
        self.position = end
        return value

    def unpack(self, fmt):
        return struct.unpack(fmt, self.take(struct.calcsize(fmt)))

    def u32(self):
        return self.unpack("<I")[0]

    def section(self, magic):
        start = self.position
        size, actual = self.unpack("<II")
        if actual != magic or size < 8 or start + size > len(self.data):
            raise CorpusError("Invalid section magic/length")
        return start + size

    def finish(self, end):
        if self.position != end:
            raise CorpusError("Section size disagrees with decoded content")


def read_state(reader):
    end = reader.section(0x01234567)
    state = dict(zip(REGISTERS, reader.unpack("<19I")))
    state["prefetch"] = list(reader.unpack("<II"))
    state["ram"] = []
    count = reader.u32()
    if count > 65536:
        raise CorpusError("Excessive RAM entries")
    for _ in range(count):
        address, word = reader.unpack("<IH")
        if address >= 0x1000000 or address & 1:
            raise CorpusError("Invalid word RAM address")
        state["ram"].extend(((address, word >> 8), (address | 1, word & 255)))
    reader.finish(end)
    return state


def decode(data):
    reader = Reader(data)
    magic, count = reader.unpack("<II")
    if magic != 0x1A3F5D71 or count > 100000:
        raise CorpusError("Invalid corpus header")
    for index in range(count):
        end = reader.section(0xABC12367)
        name_end = reader.section(0x89ABCDEF)
        name = reader.take(reader.u32()).decode("utf-8")
        reader.finish(name_end)
        initial = read_state(reader)
        final = read_state(reader)
        transactions_end = reader.section(0x456789AB)
        length, number = reader.unpack("<II")
        if number > 65536:
            raise CorpusError("Excessive bus transactions")
        transactions = []
        kinds = {1: "w", 2: "r", 3: "t", 4: "re", 5: "we"}
        for _ in range(number):
            kind, cycles = reader.unpack("<BI")
            if kind == 0:
                transactions.append(["n", cycles])
                continue
            if kind not in kinds:
                raise CorpusError("Invalid bus transaction kind")
            fc, address, value, uds, lds = reader.unpack("<IIIII")
            if uds not in (0, 1) or lds not in (0, 1):
                raise CorpusError("Invalid bus byte strobes")
            transactions.append([kinds[kind], cycles, fc, address,
                                 ".w" if uds + lds == 2 else ".b", value, uds, lds])
        reader.finish(transactions_end)
        reader.finish(end)
        yield {"index": index, "name": name, "initial": initial, "final": final,
               "length": length, "transactions": transactions}
    if reader.position != len(data):
        raise CorpusError("Trailing bytes after corpus")


def indices(count, limit):
    if limit <= 0 or limit >= count:
        return set(range(count))
    # Fixed spread across each entire group; independent of CPU outcomes.
    return {index * count // limit for index in range(limit)}


def normalize(test, group, mame=True):
    case = {**test, "group": group}
    for side in ("initial", "final"):
        state = dict(test[side])
        if mame:
            # Upstream's m_au is the next prefetch address, four bytes ahead.
            state["pc"] = (state["pc"] - 4) & 0xFFFFFFFF
        state["a7"] = state["ssp"] if state["sr"] & 0x2000 else state["usp"]
        state["ram"] = dict(state["ram"])
        case[side] = state
    case["sr_mask"] = test.get("sr_mask", 0xFFFF)
    if mame and group in ("ABCD", "SBCD", "NBCD"):
        case["sr_mask"] &= ~0x0A  # N/V undefined on the MC68000.
    if mame and group in ("DIVS", "DIVU"):
        divide_zero = any(t[0] == "r" and t[3] in (0x14, 0x16) for t in test["transactions"])
        if divide_zero:
            case["sr_mask"] &= ~0x0E  # N/Z/V undefined on divide by zero.
        elif case["final"]["sr"] & 2:
            case["sr_mask"] &= ~0x0C  # N/Z undefined on quotient overflow.
    if mame and group == "CHK":
        case["sr_mask"] &= ~0x0F  # N depends on exception path; not asserted here.
    initial = case["initial"]
    case["opcode"] = initial["prefetch"][0]
    case["unsupported"] = None
    for offset, word in enumerate(initial["prefetch"]):
        for byte_offset, value in enumerate((word >> 8, word & 255)):
            address = (initial["pc"] + offset * 2 + byte_offset) & 0xFFFFFF
            if address in initial["ram"] and initial["ram"][address] != value:
                case["unsupported"] = "initial prefetch differs from RAM; core has no loadable prefetch queue"
            initial["ram"].setdefault(address, value)
    case["expected_ram"] = {**initial["ram"], **case["final"]["ram"]}
    return case


def handwritten(path):
    spec = json.loads(Path(path).read_text())
    for index, test in enumerate(spec["cases"]):
        initial = dict.fromkeys(REGISTERS, 0)
        initial.update({"sr": 0x2700, "pc": 0xE00000, "usp": 0xE08000, "ssp": 0xE0FF00})
        initial.update(test.get("initial", {}))
        ram = dict(test.get("ram", []))
        words = test["instruction"]
        for offset, word in enumerate(words):
            address = (initial["pc"] + offset * 2) & 0xFFFFFF
            ram[address], ram[(address + 1) & 0xFFFFFF] = word >> 8, word & 255
        initial["prefetch"] = (words + [0, 0])[:2]
        initial["ram"] = list(ram.items())
        final = dict(initial)
        final.update(test["expected"])
        final["ram"] = test.get("expected_ram", [])
        normalized = normalize({"index": index, "name": test["name"], "initial": initial,
                                "final": final, "length": test.get("cycles"), "transactions": [],
                                "sr_mask": test.get("sr_mask", 0xFFFF)}, "handwritten", mame=False)
        normalized["family"] = test["family"]
        yield normalized
