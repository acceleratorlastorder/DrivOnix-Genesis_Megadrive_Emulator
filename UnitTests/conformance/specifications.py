"""Readable MC68000 specifications, independent of either emulator's results.

Arithmetic is computed with Python integers and explicit operand widths. Control
flow/exception expectations follow the MC68000 manuals, not a captured CPU run.
"""
from corpus import normalize, REGISTERS

PC, DATA, USP, SSP, HANDLER = 0x1000, 0x4000, 0x6000, 0x8000, 0x10000
X, N, Z, V, C = 16, 8, 4, 2, 1


def bytes_at(address, value, size):
    return [((address + i) & 0xFFFFFF, value >> (8 * (size - i - 1)) & 255) for i in range(size)]


def flags(value, bits, old=0x271F):
    value &= (1 << bits) - 1
    return (old & ~15) | (N if value & (1 << (bits - 1)) else 0) | (Z if value == 0 else 0)


def arithmetic(dst, src, bits, old, subtract=False, extend=False):
    mask, sign = (1 << bits) - 1, 1 << (bits - 1)
    a, b = dst & mask, src & mask
    carry_in = bool(old & X) if extend else 0
    total = a - b - carry_in if subtract else a + b + carry_in
    result = total & mask
    carry = total < 0 if subtract else total > mask
    overflow = bool(((a ^ b) if subtract else ~(a ^ b)) & (a ^ result) & sign)
    sr = (old & ~31) | (X | C if carry else 0) | (V if overflow else 0)
    sr |= N if result & sign else 0
    sr |= Z if result == 0 and (not extend or old & Z) else 0
    return (dst & ~mask | result) & 0xFFFFFFFF, sr


def condition(code, sr):
    c, v, z, n = (bool(sr & f) for f in (C, V, Z, N))
    return (True, False, not c and not z, c or z, not c, c, not z, z,
            not v, v, not n, n, n == v, n != v, not z and n == v, z or n != v)[code]


def case(group, name, words, initial=None, expected=None, ram=(), writes=(), cycles=None,
         sr_mask=0xFFFF, actions=None):
    state = dict.fromkeys(REGISTERS, 0)
    state.update(pc=PC, sr=0x271F, usp=USP, ssp=SSP)
    state.update(initial or {})
    memory = dict(ram)
    for i, word in enumerate(words):
        memory.update(bytes_at(state["pc"] + i * 2, word, 2))
    state.update(ram=list(memory.items()), prefetch=(list(words) + [0, 0])[:2])
    final = dict(state)
    final.update(pc=state["pc"] + len(words) * 2)
    final.update(expected or {})
    final["ram"] = list(writes)
    result = normalize({"index": 0, "name": name, "initial": state, "final": final,
                        "length": cycles, "sr_mask": sr_mask, "transactions": []}, group, mame=False)
    result["family"] = group.removeprefix("Test_")
    if actions is not None:
        result["actions"] = actions
    return result


def exception(group, name, opcode, vector, initial=None, saved_pc=None, mask=0xFFFF):
    state = {"sr": 0x2700, **(initial or {})}
    old = state["sr"]
    ram = bytes_at(vector * 4, HANDLER, 4)
    writes = bytes_at(SSP - 6, old, 2) + bytes_at(SSP - 4, PC + 2 if saved_pc is None else saved_pc, 4)
    return case(group, name, [opcode], state,
                {"pc": HANDLER, "ssp": SSP - 6, "sr": old & 0x27FF | 0x2000}, ram, writes, sr_mask=mask)


def instruction_cases():
    cases = []
    add = cases.append
    # Both directions, all widths, carry/borrow/overflow/zero/sign/upper bits.
    for mnemonic, base, subtract in (("ADD", 0xD200, False), ("SUB", 0x9200, True),
                                       ("CMP", 0xB200, True)):
        for size, bits in enumerate((8, 16, 32)):
            for dst, src in ((0, 0), (0, 1), (1, 1), ((1 << (bits - 1)) - 1, 1),
                             (1 << (bits - 1), 1), ((1 << bits) - 1, 1)):
                dst = dst | (0xA55A0000 if bits < 32 else 0)
                result, sr = arithmetic(dst, src, bits, 0x271F, subtract)
                if mnemonic == "CMP":
                    sr = sr & ~X | X
                add(case("Test_" + mnemonic, f"{mnemonic}.{bits} dst={dst:08x} src={src:08x}",
                         [base | size << 6], {"d0": src, "d1": dst},
                         {"d1": dst if mnemonic == "CMP" else result, "sr": sr}))
    for mnemonic, base, subtract in (("ADDI", 0x0601, False), ("SUBI", 0x0401, True), ("CMPI", 0x0C01, True)):
        for size, bits in enumerate((8, 16, 32)):
            for dst in (0, 1, 0x7FFFFFFF, 0xFFFFFFFF):
                src = 1
                result, sr = arithmetic(dst, src, bits, 0x271F, subtract)
                if mnemonic == "CMPI": sr = sr & ~X | X
                words = [base | size << 6] + ([0, src] if bits == 32 else [src])
                add(case("Test_" + mnemonic, f"{mnemonic}.{bits} boundary {dst:08x}", words,
                         {"d1": dst}, {"d1": dst if mnemonic == "CMPI" else result, "sr": sr}))
    for mnemonic, base, subtract in (("ADDQ", 0x5001, False), ("SUBQ", 0x5101, True)):
        for size, bits in enumerate((8, 16, 32)):
            for count in range(1, 9):
                dst = (1 << bits) - 1
                result, sr = arithmetic(dst, count, bits, 0x271F, subtract)
                add(case("Test_" + mnemonic, f"{mnemonic}.{bits} quick {count}",
                         [base | size << 6 | (count & 7) << 9], {"d1": dst}, {"d1": result, "sr": sr}))
        add(case("Test_" + mnemonic, mnemonic + " address register preserves flags", [base & ~7 | 0x88],
                 {"a0": DATA}, {"a0": DATA + (-8 if subtract else 8)}))
    for mnemonic, base, subtract in (("ADDA", 0xD2C0, False), ("SUBA", 0x92C0, True), ("CMPA", 0xB2C0, True)):
        for long_word in (False, True):
            for src in (1, 0x8000, 0xFFFF):
                signed_src = src if long_word or src < 0x8000 else src - 0x10000
                result, sr = arithmetic(DATA, signed_src, 32, 0x271F, subtract)
                add(case("Test_" + mnemonic, f"{mnemonic} sign extension {src:04x} long={long_word}",
                         [base | (0x100 if long_word else 0)], {"d0": src, "a1": DATA},
                         {"a1": DATA if mnemonic == "CMPA" else result,
                          "sr": sr & ~X | X if mnemonic == "CMPA" else 0x271F}))
    for mnemonic, base, subtract in (("ADDX", 0xD300, False), ("SUBX", 0x9300, True)):
        for size, bits in enumerate((8, 16, 32)):
            for old in (0x2700, 0x2704, 0x2710, 0x2714):
                for dst, src in ((0, 0), (0, 1), ((1 << bits) - 1, 0)):
                    result, sr = arithmetic(dst, src, bits, old, subtract, True)
                    add(case("Test_" + mnemonic, f"{mnemonic}.{bits} sticky zero and extend {old:04x}/{dst:x}/{src:x}",
                             [base | size << 6], {"d0": src, "d1": dst, "sr": old}, {"d1": result, "sr": sr}))
    for mnemonic, base, function in (("AND", 0xC200, lambda a, b: a & b),
                                      ("OR", 0x8200, lambda a, b: a | b),
                                      ("EOR", 0xB101, lambda a, b: a ^ b)):
        for size, bits in enumerate((8, 16, 32)):
            for a, b in ((0xA5A5A5A5, 0x5A5A5A5A), (0, 0), (0xFFFFFFFF, 0xFFFFFFFF)):
                mask = (1 << bits) - 1
                result = a & ~mask | function(a, b) & mask
                add(case("Test_" + mnemonic, f"{mnemonic}.{bits} {a:x}/{b:x}", [base | size << 6],
                         {"d0": b, "d1": a}, {"d1": result, "sr": flags(result, bits)}))
    for mnemonic, base, function in (("ANDI", 0x0201, lambda a, b: a & b),
                                      ("ORI", 0x0001, lambda a, b: a | b),
                                      ("EORI", 0x0A01, lambda a, b: a ^ b)):
        for size, bits in enumerate((8, 16, 32)):
            a, b = 0xA5A5A5A5, 0x5A5A5A5A & ((1 << bits) - 1)
            result = a & ~((1 << bits) - 1) | function(a, b) & ((1 << bits) - 1)
            words = [base | size << 6] + ([b >> 16, b & 0xFFFF] if bits == 32 else [b])
            add(case("Test_" + mnemonic, f"{mnemonic}.{bits} immediate", words, {"d1": a}, {"d1": result, "sr": flags(result, bits)}))
    for name, base, function in (("ANDI", 0x0200, lambda a, b: a & b),
                                  ("ORI", 0x0000, lambda a, b: a | b),
                                  ("EORI", 0x0A00, lambda a, b: a ^ b)):
        for sr_form in (False, True):
            for immediate in (0, 0xFFFF, 0xA515):
                sr = function(0x271F, immediate) & 0xA71F if sr_form else 0x2700 | function(31, immediate & 31)
                add(case(f"Test_{name}_TO_" + ("SR" if sr_form else "CCR"), f"{name} status mask {immediate:04x}",
                         [base | (0x7C if sr_form else 0x3C), immediate], expected={"sr": sr}))
    for name, base in (("NEG", 0x4400), ("NEGX", 0x4000), ("NOT", 0x4600), ("CLR", 0x4200), ("TST", 0x4A00)):
        for size, bits in enumerate((8, 16, 32)):
            for value in (0, 1, 0x80000080, 0xFFFFFFFF):
                if name in ("NEG", "NEGX"):
                    result, sr = arithmetic(0, value, bits, 0x271F, True, name == "NEGX")
                    result |= value & ~((1 << bits) - 1)
                else:
                    low = 0 if name == "CLR" else ~value if name == "NOT" else value
                    result = value & ~((1 << bits) - 1) | low & ((1 << bits) - 1)
                    sr = flags(result, bits)
                add(case("Test_" + name, f"{name}.{bits} {value:08x}", [base | size << 6],
                         {"d0": value}, {"d0": value if name == "TST" else result, "sr": sr}))
    # All shift/rotate types, widths, zero counts, and counts across operand size.
    for kind, name in enumerate(("AS", "LS", "ROX", "RO")):
        for left in (False, True):
            for size, bits in enumerate((8, 16, 32)):
                for encoded_count in range(128):
                    count, initial_x = encoded_count & 63, encoded_count >> 6
                    mask = (1 << bits) - 1
                    value, x, carry, overflow = 0xA5800081 & mask, initial_x, 0, False
                    for _ in range(count):
                        old_sign = value >> (bits - 1)
                        carry = old_sign if left else value & 1
                        incoming = x if kind == 2 else carry if kind == 3 else 0
                        if left:
                            value = (value << 1 | incoming) & mask
                        else:
                            incoming = old_sign if kind == 0 else incoming
                            value = value >> 1 | incoming << (bits - 1)
                        if kind < 3: x = carry
                        if kind == 0 and left: overflow |= old_sign != value >> (bits - 1)
                    if count == 0 and kind == 2: carry = x
                    sr = flags(value, bits, 0x2700) | (X if x else 0) | (C if carry else 0) | (V if overflow else 0)
                    group = f"Test_{name}L_{name}R_TO_REGISTER"
                    add(case(group, f"{name}{'L' if left else 'R'}.{bits} register count {count}, X={initial_x}",
                             [0xE220 | int(left) << 8 | size << 6 | kind << 3],
                             {"d0": 0xA5800081, "d1": count, "sr": 0x2700 | initial_x << 4},
                             {"d0": 0xA5800081 & ~mask | value, "sr": sr}))
            # Memory form always shifts a word once.
            value = 0x8001
            carry = 1
            incoming = 1 if kind in (2, 3) else 0
            result = ((value << 1 | incoming) & 0xFFFF) if left else ((value >> 1) | ((1 if kind in (0, 2, 3) else 0) << 15))
            sr = flags(result, 16, 0x2710) | C | (V if kind == 0 and left else 0)
            group = f"Test_{name}L_{name}R_TO_MEMORY"
            add(case(group, name + ("L" if left else "R") + " memory word", [0xE0D0 | kind << 9 | int(left) << 8],
                     {"a0": DATA, "sr": 0x2710}, {"sr": sr}, bytes_at(DATA, value, 2), bytes_at(DATA, result, 2)))
    # BCD specifications operate on valid packed decimal digits; N/V undefined.
    for name, opcode in (("ABCD", 0xC300), ("SBCD", 0x8300), ("NBCD", 0x4801)):
        for a, b, old in ((0, 0, 0x2704), (99, 0, 0x2714), (9, 9, 0x2704), (0, 1, 0x2700)):
            pack = lambda n: n // 10 * 16 + n % 10
            total = (a + b + bool(old & X)) if name == "ABCD" else ((0 if name == "NBCD" else a) - b - bool(old & X))
            carry = total < 0 or total > 99
            result = pack(total % 100)
            sr = old & ~31 | (X | C if carry else 0) | (Z if result == 0 and old & Z else 0)
            add(case("Test_" + name, f"{name} decimal {a}/{b}/{old:04x}", [opcode],
                     {"d0": pack(b), "d1": pack(b if name == "NBCD" else a), "sr": old},
                     {"d1": result, "sr": sr}, sr_mask=0xFFF5))
    for name, base in (("BTST", 0x0101), ("BCHG", 0x0141), ("BCLR", 0x0181), ("BSET", 0x01C1)):
        for bit in range(64):
            value = 0x80000001
            test = 1 << (bit % 32)
            result = value ^ test if name == "BCHG" else value & ~test if name == "BCLR" else value | test if name == "BSET" else value
            sr = 0x271F & ~Z | (Z if not value & test else 0)
            add(case("Test_" + name, f"{name} dynamic bit {bit}", [base], {"d0": bit, "d1": value}, {"d1": result, "sr": sr}))
        for bit in range(256):
            for memory_form in (False, True):
                value, width = (0x81, 8) if memory_form else (0x80000001, 32)
                test = 1 << (bit % width)
                result = value ^ test if name == "BCHG" else value & ~test if name == "BCLR" else value | test if name == "BSET" else value
                sr = 0x271F & ~Z | (Z if not value & test else 0)
                opcode = 0x0800 | (base & 0xC0) | (0x10 if memory_form else 1)
                add(case("Test_" + name, f"{name} immediate bit {bit}, memory={memory_form}", [opcode, bit],
                         {"a0": DATA} if memory_form else {"d1": value},
                         {"sr": sr} if memory_form else {"d1": result, "sr": sr},
                         ram=bytes_at(DATA, value, 1) if memory_form else (),
                         writes=bytes_at(DATA, result, 1) if memory_form and name != "BTST" else ()))
    for size, bits in enumerate((8, 16, 32)):
        value = 0x80000081 & ((1 << bits) - 1)
        add(case("Test_MOVE", f"MOVE.{bits} preserves upper destination", [0x1200 if bits == 8 else 0x3200 if bits == 16 else 0x2200],
                 {"d0": value, "d1": 0xA55A0000}, {"d1": 0xA55A0000 & ~((1 << bits) - 1) | value, "sr": flags(value, bits)}))
    add(case("Test_MOVEA", "MOVEA.W sign extends and preserves flags", [0x3040], {"d0": 0x8001}, {"a0": 0xFFFF8001}))
    add(case("Test_MOVEQ", "MOVEQ sign extends byte", [0x70FF], expected={"d0": 0xFFFFFFFF, "sr": 0x2718}, cycles=4))
    for group, opcode, expected in (("Test_MOVE_TO_CCR", 0x44C0, {"sr": 0x2700}),
                                     ("Test_MOVE_TO_SR", 0x46C0, {"sr": 0}),
                                     ("Test_MOVE_FROM_SR", 0x40C0, {"d0": 0x271F}),
                                     ("Test_MOVE_USP", 0x4E68, {"a0": USP})):
        add(case(group, group + " defined status/bank transfer", [opcode], expected=expected))
    for bits, opcode in ((16, 0x4880), (32, 0x48C0)):
        add(case("Test_EXT", f"EXT.{bits} sign extends", [opcode], {"d0": 0xA55A8080},
                 {"d0": 0xA55AFF80 if bits == 16 else 0xFFFF8080, "sr": 0x2718}))
    add(case("Test_SWAP", "SWAP both words", [0x4840], {"d0": 0x12345678}, {"d0": 0x56781234, "sr": 0x2710}, cycles=4))
    add(case("Test_EXG", "EXG data/address registers preserves flags", [0xC188], {"d0": 0xABCDEF01, "a0": DATA}, {"d0": DATA, "a0": 0xABCDEF01}))
    for signed, group, opcode in ((False, "MULU", 0xC2C0), (True, "MULS", 0xC3C0)):
        for a, b in ((0, 0xFFFF), (256, 256), (0x8000, 0xFFFF), (0xFFFF, 0xFFFF)):
            operand = lambda n: n - 65536 if signed and n & 0x8000 else n
            result = operand(a) * operand(b) & 0xFFFFFFFF
            add(case("Test_" + group, f"{group} full product {a:x}/{b:x}", [opcode], {"d0": a, "d1": b}, {"d1": result, "sr": flags(result, 32)}))
    for signed, group, opcode in ((False, "DIVU", 0x82C0), (True, "DIVS", 0x83C0)):
        for dividend, divisor in ((8, 4), (0, 4), (0xFFFF, 1), (0xFFFFFFF8, 4), (0x80000000, 0xFFFF)):
            a = dividend - 0x100000000 if signed and dividend & 0x80000000 else dividend
            b = divisor - 0x10000 if signed and divisor & 0x8000 else divisor
            quotient = (abs(a) // abs(b)) * (-1 if (a < 0) != (b < 0) else 1)
            overflow = not (-32768 <= quotient <= 32767) if signed else quotient > 65535
            result = dividend if overflow else ((a - quotient * b) & 0xFFFF) << 16 | quotient & 0xFFFF
            sr = 0x2712 if overflow else flags(quotient, 16)
            add(case("Test_" + group, f"{group} quotient/remainder {a}/{b}", [opcode], {"d0": divisor, "d1": dividend},
                     {"d1": result, "sr": sr}, sr_mask=0xFFF3 if overflow else 0xFFFF))
        add(exception("Test_" + group, group + " divide by zero", opcode, 5, {"d0": 0, "d1": 8}, mask=0xFFF1))
    # Every condition under every NZVC combination, including DBcc terminal count.
    for code in range(16):
        for low in range(32):
            sr = 0x2700 | low
            truth = condition(code, sr)
            if code >= 2:
                add(case("Test_Bcc", f"Bcc condition={code} NZVC={low:x}", [0x6006 | code << 8], {"sr": sr}, {"pc": PC + (8 if truth else 2)}))
            add(case("Test_Scc", f"Scc condition={code} NZVC={low:x}", [0x50C0 | code << 8], {"sr": sr, "d0": 0x12345678},
                     {"d0": 0x123456FF if truth else 0x12345600}))
            for counter in (0, 1):
                add(case("Test_DBcc", f"DBcc condition={code} NZVC={low:x} count={counter}", [0x50C8 | code << 8, 6],
                         {"sr": sr, "d0": 0xA55A0000 | counter},
                         {"d0": 0xA55A0000 | (counter if truth else counter - 1 & 0xFFFF),
                          "pc": PC + (8 if not truth and counter != 0 else 4)}))
    add(case("Test_BRA", "BRA.W base is extension word", [0x6000, 6], expected={"pc": PC + 8}))
    add(case("Test_BSR", "BSR.W pushes PC after extension", [0x6100, 6], expected={"pc": PC + 8, "ssp": SSP - 4}, writes=bytes_at(SSP - 4, PC + 4, 4)))
    add(case("Test_JMP", "JMP absolute long", [0x4EF9, 1, 0], expected={"pc": HANDLER}))
    add(case("Test_JSR", "JSR pushes next instruction address", [0x4EB9, 1, 0], expected={"pc": HANDLER, "ssp": SSP - 4}, writes=bytes_at(SSP - 4, PC + 6, 4)))
    add(case("Test_LEA", "LEA computes address without changing flags", [0x41F9, 1, 0], expected={"a0": HANDLER}))
    add(case("Test_PEA", "PEA pushes effective address", [0x4879, 1, 0], expected={"ssp": SSP - 4}, writes=bytes_at(SSP - 4, HANDLER, 4)))
    add(case("Test_LINK", "LINK saves frame and sign extends displacement", [0x4E50, 0xFFF0], {"a0": DATA},
             {"a0": SSP - 4, "ssp": SSP - 20}, writes=bytes_at(SSP - 4, DATA, 4)))
    add(case("Test_UNLK", "UNLK restores frame and stack", [0x4E58], {"a0": DATA},
             {"a0": 0x12345678, "ssp": DATA + 4}, ram=bytes_at(DATA, 0x12345678, 4)))
    add(case("Test_RTS", "RTS restores big endian PC", [0x4E75], expected={"pc": HANDLER, "ssp": SSP + 4}, ram=bytes_at(SSP, HANDLER, 4)))
    frame = bytes_at(SSP, 0x0015, 2) + bytes_at(SSP + 2, HANDLER, 4)
    add(case("Test_RTE", "RTE restores user SR PC and banked stack", [0x4E73], expected={"pc": HANDLER, "sr": 0x0015, "ssp": SSP + 6}, ram=frame))
    add(case("Test_RTR", "RTR restores only CCR and PC", [0x4E77], expected={"pc": HANDLER, "sr": 0x2715, "ssp": SSP + 6}, ram=frame))
    add(case("Test_MOVEM", "MOVEM.L stores D0/D1 in order", [0x48D0, 3], {"a0": DATA, "d0": 0x12345678, "d1": 0x89ABCDEF},
             writes=bytes_at(DATA, 0x12345678, 4) + bytes_at(DATA + 4, 0x89ABCDEF, 4)))
    add(case("Test_MOVEP", "MOVEP.L writes alternating bytes", [0x01C8, 0], {"a0": DATA, "d0": 0x12345678},
             writes=[(DATA, 0x12), (DATA + 2, 0x34), (DATA + 4, 0x56), (DATA + 6, 0x78)]))
    add(case("Test_CMPM", "CMPM.W postincrements both addresses", [0xB348], {"a0": DATA, "a1": DATA + 4},
             {"a0": DATA + 2, "a1": DATA + 6, "sr": 0x2719}, ram=bytes_at(DATA, 1, 2) + bytes_at(DATA + 4, 0, 2)))
    for value in (0, 0x7F, 0x80, 0xFF):
        add(case("Test_TAS", f"TAS changes only low byte {value:x}", [0x4AC0], {"d0": 0xA55A0000 | value},
                 {"d0": 0xA55A0080 | value, "sr": flags(value, 8)}))
    add(case("Test_CHK", "CHK in range preserves X", [0x4380], {"d0": 9, "d1": 4}, sr_mask=0xFFF0))
    add(exception("Test_CHK", "CHK negative raises vector 6 and sets N", 0x4380, 6, {"d0": 9, "d1": 0xFFFFFFFF, "sr": 0x2708}, mask=0xFFF8))
    add(exception("Test_CHK", "CHK above bound raises vector 6 and clears N", 0x4380, 6, {"d0": 9, "d1": 10}, mask=0xFFF8))
    for vector in range(32, 48):
        add(exception("Test_TRAP", f"TRAP #{vector - 32} saves next PC", 0x4E40 + vector - 32, vector))
    add(exception("Test_ILLEGAL", "ILLEGAL saves faulting instruction PC", 0x4AFC, 4, saved_pc=PC))
    add(exception("Test_ILLEGAL", "line A exception", 0xA000, 10, saved_pc=PC))
    add(exception("Test_ILLEGAL", "line F exception", 0xF000, 11, saved_pc=PC))
    add(case("Test_TRAPV", "TRAPV with clear V preserves state", [0x4E76], {"sr": 0x2700}))
    add(exception("Test_TRAPV", "TRAPV with V raises vector 7", 0x4E76, 7, {"sr": 0x2702}))
    add(case("Test_STOP", "STOP consumes extension and loads SR", [0x4E72, 0x2700], expected={"sr": 0x2700, "stop": True}))
    add(exception("Test_STOP", "user STOP privilege violation precedes extension", 0x4E72, 8, {"sr": 0}, saved_pc=PC))
    add(case("Test_RESET", "RESET preserves CPU registers and flags", [0x4E70], cycles=132))
    add(exception("Test_RESET", "user RESET privilege violation", 0x4E70, 8, {"sr": 0}, saved_pc=PC))
    for group, opcode in (("Test_RTE", 0x4E73), ("Test_MOVE_TO_SR", 0x46C0),
                           ("Test_ORI_TO_SR", 0x007C), ("Test_ANDI_TO_SR", 0x027C),
                           ("Test_EORI_TO_SR", 0x0A7C), ("Test_MOVE_USP", 0x4E60), ("Test_MOVE_USP", 0x4E68)):
        add(exception(group, f"user opcode {opcode:04x} privilege violation", opcode, 8, {"sr": 0}, saved_pc=PC))
    add(case("Test_NOP", "NOP preserves all flags", [0x4E71], cycles=4))
    cases.extend(addressing_cases())
    cases.extend(timing_cases())
    for index, item in enumerate(cases):
        item["index"] = index
    return cases


def timing_cases():
    """Motorola 68000 timing totals, also checked by independent MAME vectors.

    These deliberately cover exact encodings where pinned Musashi's tables are
    inaccurate; they are never obtained from DrivOnix or a recorded baseline.
    """
    cases = []
    initial = {"d0": 1, "d1": 3, "a0": 1, "a1": 3, "sr": 0x2700}
    for name, base, subtract in (("ADD", 0xD200, False), ("SUB", 0x9200, True)):
        result, sr = arithmetic(3, 1, 32, 0x2700, subtract)
        for ea, source in ((0, "Dn"), (8, "An")):
            cases.append(case("Test_" + name, f"{name}.L {source},Dn takes 8 cycles",
                              [base | 0x80 | ea], initial, {"d1": result, "sr": sr}, cycles=8))
        for bits, size in ((8, 0), (16, 0x40)):
            result, sr = arithmetic(3, 1, bits, 0x2700, subtract)
            cases.append(case("Test_" + name, f"{name}.{bits} immediate-EA source takes 8 cycles",
                              [base | size | 0x3C, 1], initial, {"d1": result, "sr": sr}, cycles=8))
    for name, base, subtract in (("ADDA", 0xD3C0, False), ("SUBA", 0x93C0, True)):
        result = 2 if subtract else 4
        for ea, source in ((0, "Dn"), (8, "An")):
            cases.append(case("Test_" + name, f"{name}.L {source},An takes 8 cycles",
                              [base | ea], initial, {"a1": result}, cycles=8))
        cases.append(case("Test_" + name, f"{name}.W immediate-EA source takes 12 cycles",
                          [(base & ~0x100) | 0x3C, 1], initial, {"a1": result}, cycles=12))
    for name, base in (("AND", 0xC200), ("OR", 0x8200)):
        result = 1 if name == "AND" else 3
        cases.append(case("Test_" + name, f"{name}.L Dn,Dn takes 8 cycles", [base | 0x80],
                          initial, {"d1": result, "sr": flags(result, 32, 0x2700)}, cycles=8))
        for bits, size in ((8, 0), (16, 0x40)):
            cases.append(case("Test_" + name, f"{name}.{bits} immediate-EA source takes 8 cycles",
                              [base | size | 0x3C, 1], initial,
                              {"d1": result, "sr": flags(result, bits, 0x2700)}, cycles=8))
    for name, opcode, result in (("ADDQ", 0x5249, 4), ("SUBQ", 0x5349, 2)):
        cases.append(case("Test_" + name, f"{name}.W #1,An takes 8 cycles and preserves flags",
                          [opcode], initial, {"a1": result}, cycles=8))
    # Invalid decimal digit bytes still have repeatable correction-network
    # behavior; Musashi's decimal comparisons do not reproduce these cases.
    cases.append(case("Test_ABCD", "ABCD invalid digits 5D+36+X retain 9A without carry", [0xC300],
                      {"d0": 0x36, "d1": 0x5D, "sr": 0x2710},
                      {"d1": 0x9A, "sr": 0x2700}, sr_mask=0xFFF5, cycles=6))
    cases.append(case("Test_SBCD", "SBCD invalid positive digits DA-01 retain D9 without carry", [0x8300],
                      {"d0": 1, "d1": 0xDA, "sr": 0x2700},
                      {"d1": 0xD9, "sr": 0x2700}, sr_mask=0xFFF5, cycles=6))
    cases.append(case("Test_SBCD", "SBCD invalid digits 10-0F-X retain FA with decimal borrow", [0x8300],
                      {"d0": 0x0F, "d1": 0x10, "sr": 0x2710},
                      {"d1": 0xFA, "sr": 0x2711}, sr_mask=0xFFF5, cycles=6))
    cases.append(case("Test_NBCD", "NBCD invalid FF with X produces 9A with carry", [0x4800],
                      {"d0": 0xFF, "sr": 0x2710},
                      {"d0": 0x9A, "sr": 0x2711}, sr_mask=0xFFF5, cycles=6))
    cases.append(case("Test_ANDI", "ANDI.L immediate to data register takes 16 cycles", [0x0281, 0, 1],
                      {"d1": 3, "sr": 0x2700}, {"d1": 1, "sr": 0x2700}, cycles=16))
    for name, dynamic, immediate, low_cycles in (("BCHG", 0x0141, 0x0841, 6),
                                                ("BCLR", 0x0181, 0x0881, 8),
                                                ("BSET", 0x01C1, 0x08C1, 6)):
        for bit in (1, 17):
            value = 0 if name == "BCLR" else 1 << bit
            for static in (False, True):
                words = [immediate, bit] if static else [dynamic]
                state = {"d0": bit if not static else 0, "d1": 0, "sr": 0x2700}
                cases.append(case("Test_" + name, f"{name} {'immediate' if static else 'dynamic'} register bit {bit} timing",
                                  words, state, {"d1": value, "sr": 0x2704},
                                  cycles=low_cycles + (4 if static else 0) + (2 if bit >= 16 else 0)))
    cases.append(case("Test_BTST", "BTST dynamic immediate EA takes 10 cycles", [0x013C, 0],
                      {"d0": 0, "sr": 0x2700}, {"sr": 0x2704}, cycles=10))
    for name, opcode in (("DIVU", 0x82C0), ("DIVS", 0x83C0)):
        item = exception("Test_" + name, name + " divide-zero clears incoming carry before stacking SR",
                         opcode, 5, {"d0": 0, "d1": 8, "sr": 0x2701}, mask=0xFFF1)
        item["final"]["sr"] &= ~1
        item["expected_ram"][SSP - 5] &= ~1
        cases.append(item)
    for bits, opcode in ((16, 0x4278), (32, 0x42B8)):
        frame = bytes_at(SSP - 14, 21, 2) + bytes_at(SSP - 12, DATA + 1, 4)
        frame += bytes_at(SSP - 8, opcode, 2) + bytes_at(SSP - 6, 0x2701, 2) + bytes_at(SSP - 4, PC + 4, 4)
        item = case("Test_CLR", f"CLR.{bits} odd memory faults on dummy read before flags change",
                    [opcode, DATA + 1], {"sr": 0x2701},
                    {"pc": HANDLER, "ssp": SSP - 14, "sr": 0x2701}, ram=bytes_at(12, HANDLER, 4),
                    writes=frame, cycles=62)
        item["memory_masks"] = {SSP - 14: 0, SSP - 13: 31}
        cases.append(item)
    for opcode in range(0xF620, 0xF628):
        cases.append(exception("Test_ILLEGAL", f"68040 MOVE16 {opcode:04x} raises line F on MC68000",
                               opcode, 11, saved_pc=PC))
    for words, initial, changes, target, saved, clocks, label in (
            ([0x40D8], {"a0": DATA + 1}, {"a0": DATA + 3}, DATA + 1, PC + 2, 58, "postincrement"),
            ([0x40E0], {"a0": DATA + 3}, {"a0": DATA + 1}, DATA + 1, PC + 4, 60, "predecrement"),
            ([0x40F8, DATA + 1], {}, {}, DATA + 1, PC + 4, 62, "absolute word"),
            ([0x40F9, 0, DATA + 1], {}, {}, DATA + 1, PC + 6, 66, "absolute long")):
        frame = bytes_at(SSP - 14, 21, 2) + bytes_at(SSP - 12, target, 4)
        frame += bytes_at(SSP - 8, words[0], 2) + bytes_at(SSP - 6, 0x2701, 2) + bytes_at(SSP - 4, saved, 4)
        item = case("Test_MOVE_FROM_SR", "MOVE SR " + label + " faults on destination dummy read",
                    words, {"sr": 0x2701, **initial},
                    {"pc": HANDLER, "ssp": SSP - 14, "sr": 0x2701, **changes},
                    ram=bytes_at(12, HANDLER, 4), writes=frame, cycles=clocks)
        item["memory_masks"] = {SSP - 14: 0, SSP - 13: 31}
        cases.append(item)
    cases.append(case("Test_LINK", "LINK A7 stores original stack pointer before decrement", [0x4E57, 0],
                      {"sr": 0x2700}, {"ssp": SSP - 4}, writes=bytes_at(SSP - 4, SSP, 4), cycles=16))
    for value, bound, clocks, negative, label in ((1, 0, 42, False, "upper bound"),
                                                (0xFFFE, 0, 44, True, "negative lower check"),
                                                (0xFFFF, 0xFFFE, 42, True, "negative upper check")):
        status = 0x2705 | (N if negative else 0)
        frame = bytes_at(SSP - 6, status, 2) + bytes_at(SSP - 4, PC + 2, 4)
        item = case("Test_CHK", "CHK memory " + label + " trap timing", [0x4190],
                    {"sr": 0x2705, "a0": DATA, "d0": value},
                    {"pc": HANDLER, "ssp": SSP - 6, "sr": status},
                    ram=bytes_at(24, HANDLER, 4) + bytes_at(DATA, bound, 2), writes=frame,
                    cycles=clocks, sr_mask=0xFFF8)
        item["memory_masks"] = {SSP - 5: 0xF8}
        cases.append(item)
    for name, opcode, saved in (("JMP", 0x4EE8, PC + 2), ("JSR", 0x4EA8, PC + 4)):
        frame = bytes_at(SSP - 14, 22, 2) + bytes_at(SSP - 12, DATA + 1, 4)
        frame += bytes_at(SSP - 8, opcode, 2) + bytes_at(SSP - 6, 0x2700, 2) + bytes_at(SSP - 4, saved, 4)
        item = case("Test_" + name, name + " odd displacement target faults in 60 cycles", [opcode, 1],
                    {"sr": 0x2700, "a0": DATA}, {"pc": HANDLER, "ssp": SSP - 14},
                    ram=bytes_at(12, HANDLER, 4), writes=frame, cycles=60)
        item["memory_masks"] = {SSP - 14: 0, SSP - 13: 31}
        cases.append(item)
    for words, source, target, saved, clocks, status, label in (
            ([0x2140, 1], {"a0": DATA, "d0": 0x12345678}, DATA + 1, PC + 4, 62, 0x2703, "register to displacement"),
            ([0x2180, 0x1001], {"a0": DATA, "d0": 0x12345678}, DATA + 1, PC + 4, 64, 0x2703, "register to index"),
            ([0x20BC, 0x1234, 0x5678], {"a0": DATA + 1}, DATA + 1, PC + 8, 66, 0x270F, "immediate to indirect"),
            ([0x20FC, 0x1234, 0x5678], {"a0": DATA + 1}, DATA + 1, PC + 8, 66, 0x270F, "immediate to postincrement")):
        frame = bytes_at(SSP - 14, 5, 2) + bytes_at(SSP - 12, target, 4)
        frame += bytes_at(SSP - 8, words[0], 2) + bytes_at(SSP - 6, status, 2) + bytes_at(SSP - 4, saved, 4)
        item = case("Test_MOVE", "MOVE.L " + label + " fault preserves staged condition flags", words,
                    {"sr": 0x270F, **source}, {"pc": HANDLER, "ssp": SSP - 14, "sr": status},
                    ram=bytes_at(12, HANDLER, 4), writes=frame, cycles=clocks)
        item["memory_masks"] = {SSP - 14: 0, SSP - 13: 31}
        cases.append(item)
    return cases


def addressing_cases():
    cases = []
    for bits, base in ((8, 0x1200), (16, 0x3200), (32, 0x2200)):
        size, mask = bits // 8, (1 << bits) - 1
        modes = [
            ("D0", 0, [], {}, None, {}),
            ("(A0)", 0x10, [], {"a0": DATA}, DATA, {}),
            ("(A0)+", 0x18, [], {"a0": DATA}, DATA, {"a0": DATA + size}),
            ("-(A0)", 0x20, [], {"a0": DATA}, DATA - size, {"a0": DATA - size}),
            ("positive d16(A0)", 0x28, [16], {"a0": DATA}, DATA + 16, {}),
            ("negative d16(A0)", 0x28, [0xFFF0], {"a0": DATA}, DATA - 16, {}),
            ("D2.W signed index", 0x30, [0x2001], {"a0": DATA, "d2": 0x1234FFFF}, DATA, {}),
            ("D2.L signed index", 0x30, [0x2802], {"a0": DATA, "d2": 0xFFFFFFFE}, DATA, {}),
            ("A2.W signed index", 0x30, [0xA001], {"a0": DATA, "a2": 0x1234FFFF}, DATA, {}),
            ("A2.L signed index", 0x30, [0xA802], {"a0": DATA, "a2": 0xFFFFFFFE}, DATA, {}),
            ("absolute word", 0x38, [DATA], {}, DATA, {}),
            ("absolute word sign extension", 0x38, [0xFFF0], {}, 0xFFFFF0, {}),
            ("absolute long", 0x39, [0, DATA], {}, DATA, {}),
            ("positive d16(PC)", 0x3A, [DATA - PC - 2], {}, DATA, {}),
            ("negative d16(PC)", 0x3A, [0xFFF0], {}, PC + 2 - 16, {}),
            ("D2.W PC index", 0x3B, [0x2010], {"d2": 0x12340010}, PC + 34, {}),
            ("(A7)+", 0x1F, [], {}, SSP, {"ssp": SSP + (2 if bits == 8 else size)}),
            ("-(A7)", 0x27, [], {}, SSP - (2 if bits == 8 else size), {"ssp": SSP - (2 if bits == 8 else size)}),
        ]
        if bits != 8: modes.append(("A0", 8, [], {}, None, {}))
        for value in (0, 1, 1 << (bits - 1), mask):
            for name, ea, extension, initial, address, changed in modes:
                initial = {"d1": 0xA55A0000, **initial}
                if ea == 0: initial["d0"] = value
                if ea == 8: initial["a0"] = value
                result = initial["d1"] & ~mask | value
                expected = {"d1": result, "sr": flags(value, bits), **changed}
                cases.append(case("Test_MOVE", f"MOVE.{bits} source {name} value={value:x}", [base | ea, *extension],
                                  initial, expected, ram=bytes_at(address, value, size) if address is not None else ()))
            words = ([value >> 16, value & 0xFFFF] if bits == 32 else [value])
            cases.append(case("Test_MOVE", f"MOVE.{bits} immediate value={value:x}", [base | 0x3C, *words],
                              {"d1": 0xA55A0000}, {"d1": 0xA55A0000 & ~mask | value, "sr": flags(value, bits)}))
        # The same legal alterable modes as destinations, with byte/word/long data.
        for name, ea, extension, initial, address, changed in modes:
            if address is None or ea in (0x3A, 0x3B): continue
            value = mask
            opcode = (0x1000 if bits == 8 else 0x3000 if bits == 16 else 0x2000) | (ea >> 3) << 6 | (ea & 7) << 9
            cases.append(case("Test_MOVE", f"MOVE.{bits} destination {name}", [opcode, *extension],
                              {"d0": value, **initial}, {"sr": flags(value, bits), **changed}, writes=bytes_at(address, value, size)))
    return cases


def sequence_cases():
    """Interactions observable at CPU boundaries, with exact final state/RAM."""
    cases = []
    add = cases.append
    add(case("sequences", "JSR followed by RTS restores the calling stack", [0x4EB9, 1, 0],
             expected={"pc": PC + 6}, ram=bytes_at(HANDLER, 0x4E75, 2),
             writes=bytes_at(SSP - 4, PC + 6, 4), actions=[0, 0]))
    add(case("sequences", "LINK followed by UNLK restores frame and stack", [0x4E50, 0xFFF0, 0x4E58],
             {"a0": DATA}, {"pc": PC + 6}, writes=bytes_at(SSP - 4, DATA, 4), actions=[0, 0]))
    add(case("sequences", "MOVEM predecrement then postincrement restores both data registers", [0x48E7, 0xC000, 0x4CDF, 3],
             {"d0": 0x12345678, "d1": 0x89ABCDEF}, {"pc": PC + 8},
             writes=bytes_at(SSP - 8, 0x12345678, 4) + bytes_at(SSP - 4, 0x89ABCDEF, 4), actions=[0, 0]))
    add(case("sequences", "ADDX carries between two byte operations and clears sticky Z", [0xD300, 0xD702],
             {"d0": 1, "d1": 255, "sr": 0x2704}, {"d1": 0, "d3": 1, "sr": 0x2700}, actions=[0, 0]))
    for user in (False, True):
        old_sr = 0 if user else 0x2000
        frame = bytes_at(SSP - 6, old_sr, 2) + bytes_at(SSP - 4, PC + 2, 4)
        add(case("sequences", f"TRAP then RTE restores {'user' if user else 'supervisor'} execution", [0x4E40],
                 {"sr": old_sr}, {"pc": PC + 2},
                 ram=bytes_at(32 * 4, HANDLER, 4) + bytes_at(HANDLER, 0x4E73, 2), writes=frame, actions=[0, 0]))
    for level in range(1, 8):
        for user in (False, True):
            old_sr = 0 if user else 0x2000
            address = HANDLER + level * 16
            frame = bytes_at(SSP - 6, old_sr, 2) + bytes_at(SSP - 4, PC, 4)
            add(case("sequences", f"IRQ {level} enters supervisor stack from {'user' if user else 'supervisor'}", [0x4E71],
                     {"sr": old_sr}, {"pc": address + 2, "sr": 0x2000 | level << 8, "ssp": SSP - 6},
                     ram=bytes_at((24 + level) * 4, address, 4) + bytes_at(address, 0x4E71, 2),
                     writes=frame, cycles=48, actions=[level + 1, 0]))
    for level in range(1, 7):
        add(case("sequences", f"IRQ {level} remains masked at interrupt mask 7", [0x4E71],
                 {"sr": 0x2700}, cycles=4, actions=[level + 1, 0]))
    frame = bytes_at(SSP - 6, 0x2000, 2) + bytes_at(SSP - 4, PC + 4, 4)
    add(case("sequences", "STOP wakes on IRQ then RTE resumes at following instruction", [0x4E72, 0x2000, 0x4E71],
             {"sr": 0x2000}, {"pc": PC + 4, "stop": False},
             ram=bytes_at(30 * 4, HANDLER, 4) + bytes_at(HANDLER, 0x4E73, 2), writes=frame, actions=[0, 7, 0]))
    add(case("sequences", "STOP remains stopped without interrupt", [0x4E72, 0x2700],
             {"sr": 0x2700}, {"pc": PC + 4, "stop": True}, actions=[0, 0]))
    for sr in (0xA700, 0x8000):
        frame = bytes_at(SSP - 6, sr, 2) + bytes_at(SSP - 4, PC + 2, 4)
        add(case("sequences", f"trace after NOP saves next PC and clears T ({sr:04x})", [0x4E71],
                 {"sr": sr}, {"pc": HANDLER, "sr": (sr & ~0x8000) | 0x2000, "ssp": SSP - 6},
                 ram=bytes_at(9 * 4, HANDLER, 4), writes=frame, cycles=38))
    reset = case("sequences", "external RESET loads vectors and defined SR control bits", [0x4E71],
                 {"d0": 0x12345678, "a0": DATA, "sr": 0xA01F},
                 {"pc": HANDLER, "ssp": 0x9000, "sr": 0x2700},
                 ram=bytes_at(0, 0x9000, 4) + bytes_at(4, HANDLER, 4), actions=[9], sr_mask=0xFFE0)
    reset["field_masks"] = {**{f"d{i}": 0 for i in range(8)}, **{f"a{i}": 0 for i in range(7)}, "usp": 0}
    add(reset)
    # The 68000 prefetches the next opcode before a write to that same address.
    add(case("sequences", "self modifying write does not replace already prefetched next opcode", [0x3080, 0x7201],
             {"d0": 0x7202, "a0": PC + 2}, {"d1": 1, "pc": PC + 4, "sr": 0x2710},
             writes=bytes_at(PC + 2, 0x7202, 2), actions=[0, 0]))
    for user in (False, True):
        for bits in (16, 32):
            for write in (False, True):
                opcode = (0x3080 if bits == 16 else 0x2080) if write else (0x3010 if bits == 16 else 0x2010)
                old_sr, address = (0 if user else 0x2700), DATA + 1
                # MC68000 UM 6.3.9.1/6.3.10, figure 6-7: seven-word frame.
                # I/N describes processor activity; operand accesses occur while
                # processing an instruction. FC distinguishes user/supervisor data.
                ssw = (0 if write else 16) | (1 if user else 5)
                frame = (bytes_at(SSP - 14, ssw, 2) + bytes_at(SSP - 12, address, 4) +
                         bytes_at(SSP - 8, opcode, 2) + bytes_at(SSP - 6, old_sr, 2) + bytes_at(SSP - 4, 0, 4))
                item = case("sequences", f"odd {bits}-bit {'write' if write else 'read'} address error, user={user}", [opcode],
                            {"a0": address, "d0": 0x12345678, "sr": old_sr},
                            {"pc": HANDLER, "ssp": SSP - 14, "sr": old_sr | 0x2000},
                            ram=bytes_at(12, HANDLER, 4), writes=frame)
                # Saved PC depends on the prefetch stage (documented +2..+10),
                # and upper SSW bits are not defined. Assert the defined fields.
                item["memory_masks"] = {SSP - 14: 0, SSP - 13: 31, **{SSP - 4 + n: 0 for n in range(4)}}
                add(item)
    for opcode, extension in ((0x31C0, [PC + 4]), (0x33C0, [0, PC + 6])):
        address = PC + 2 + 2 * len(extension)
        add(case("sequences", f"self modifying MOVE extension words preserve next prefetched opcode ({opcode:04x})",
                 [opcode, *extension, 0x7201], {"d0": 0x7202},
                 {"d1": 1, "pc": address + 2, "sr": 0x2710},
                 writes=bytes_at(address, 0x7202, 2), actions=[0, 0]))
    for index, item in enumerate(cases): item["index"] = index
    return cases
