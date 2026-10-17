"""Deterministic inputs for every possible MC68000 opcode word.

No DrivOnix output is used to construct expected results. The pinned reference
supplies differential expectations; readable specifications remain authoritative
where the reference itself has documented defects.
"""
import copy
from specifications import PC, DATA, SSP, USP, HANDLER, bytes_at, case

PATTERNS = (0, 1, 0xFFFFFFFF, 0x7FFFFFFF, 0x80000000, 0x8000, 0xFFFF, 0x5555AAAA)


def word_source(test):
    """MC68000 data EA, used only to recognize the oracle's signed divide edge."""
    state, opcode = test["initial"], test["opcode"]
    memory = state["ram"]
    word = lambda address: memory.get(address & 0xFFFFFF, 0) << 8 | memory.get((address + 1) & 0xFFFFFF, 0)
    signed = lambda value, bits: (value & ((1 << bits) - 1)) - (1 << bits) if value & (1 << (bits - 1)) else value
    mode, register = opcode >> 3 & 7, opcode & 7
    if mode == 0: return state[f"d{register}"] & 0xFFFF
    base = state.get(f"a{register}", state["ssp"] if state["sr"] & 0x2000 else state["usp"])
    extension = word(state["pc"] + 2)
    if mode in (2, 3): address = base
    elif mode == 4: address = base - 2
    elif mode == 5: address = base + signed(extension, 16)
    elif mode == 7 and register == 0: address = signed(extension, 16)
    elif mode == 7 and register == 1: address = extension << 16 | word(state["pc"] + 4)
    elif mode == 7 and register == 2: address = state["pc"] + 2 + signed(extension, 16)
    elif mode == 7 and register == 4: return extension
    elif mode == 6 or mode == 7 and register == 3:
        index_register = extension >> 12 & 7
        index = state.get(f"a{index_register}", state["ssp"] if state["sr"] & 0x2000 else state["usp"]) if extension & 0x8000 else state[f"d{index_register}"]
        if not extension & 0x800: index = signed(index & 0xFFFF, 16)
        address = (state["pc"] + 2 if mode == 7 else base) + signed(extension & 255, 8) + index
    else: return None
    return word(address)


def vectors():
    result = []
    for vector in (*range(2, 12), *range(24, 48)):
        result += bytes_at(vector * 4, HANDLER + vector * 16, 4)
        result += bytes_at(HANDLER + vector * 16, 0x4E714E71, 4)
    return result


def inputs(opcodes, pattern_index=0):
    pattern = PATTERNS[pattern_index]
    initial = {f"d{i}": pattern for i in range(8)}
    initial.update({f"a{i}": DATA + i * 0x100 for i in range(7)})
    initial["sr"] = 0x2700 | (pattern_index * 5 & 31)
    # Varied extension words exercise immediate values, displacement/index,
    # absolute addresses, and MOVEM masks without an unbounded instruction stream.
    extensions = (0, 0x2000, 0, 0x3000) if pattern_index == 0 else (
        (0x0001, 0x0008, 0x0000, 0x0000) if pattern_index & 1 else (0xFFFF, 0xFFF8, 0x8000, 0x0000))
    ram = vectors()
    ram += bytes_at(0, SSP, 4) + bytes_at(4, PC, 4)
    for register in range(7):
        for offset in (-4, 0, 4):
            ram += bytes_at(DATA + register * 0x100 + offset, pattern, 4)
    # Valid return/exception stack inputs; other operand reads are defined as zero.
    ram += bytes_at(SSP, 0x2700, 2) + bytes_at(SSP + 2, PC + 16, 4)
    for opcode in opcodes:
        result = case("decode_sweep", f"opcode {opcode:04x}, pattern {pattern_index}",
                      [opcode, *extensions], initial, ram=ram)
        result["index"] = pattern_index * 65536 + opcode
        result["opcode"] = opcode
        yield result


def expected_from_reference(test, actual, catalog):
    result = copy.deepcopy(test)
    result["final"] = {field: value for field, value in actual.items() if field not in ("cycles", "ram")}
    result["expected_ram"] = {**test["initial"]["ram"], **actual["ram"]}
    result["length"] = actual["cycles"]
    opcode = test["opcode"]
    family = catalog.get(f"{opcode:04x}", "None").split()[0]
    if family in ("ABCD", "SBCD", "NBCD"):
        result["sr_mask"] &= ~0x0A
    if family == "CHK":
        result["sr_mask"] &= ~0x0F
    if family in ("DIVS", "DIVU"):
        # Motorola PRM 4-92/4-96: C is always cleared, including overflow.
        # Pinned Musashi leaves C set on some overflow paths. Do not copy it.
        address_error = actual["pc"] == HANDLER + 3 * 16
        if not address_error: result["final"]["sr"] &= ~1
        zero = actual["pc"] == HANDLER + 5 * 16
        if zero:
            result["sr_mask"] &= ~0x0E
        elif not address_error and actual["sr"] & 2:
            result["sr_mask"] &= ~0x0C
        # Musashi uses fixed rather than operand-dependent MUL/DIV cycle totals.
        # Correct timing remains asserted by the independent MAME corpus.
        result["length"] = None
        if family == "DIVS" and not address_error:
            destination = opcode >> 9 & 7
            if word_source(test) == 0xFFFF and test["initial"][f"d{destination}"] == 0x80000000:
                result["final"][f"d{destination}"] = 0x80000000
                result["final"]["sr"] |= 2
                result["sr_mask"] &= ~0x0C
    if family in ("MULS", "MULU"):
        result["length"] = None
    return result
