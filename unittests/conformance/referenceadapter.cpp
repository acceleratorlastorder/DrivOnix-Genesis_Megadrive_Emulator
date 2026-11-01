// Test-only independent oracle. Never linked into the emulator or cpu_under_test.
#include "../../m68k/m68k.hpp"
#include "../cputestmemory.hpp"

extern "C" {
#include "m68k.h"
int drivonix_reference_stopped();
int drivonix_reference_step(int traceBoundary);
void drivonix_reference_program_fault(unsigned target, unsigned savedPC, unsigned opcode);
}

extern "C" unsigned int m68k_read_memory_8(unsigned int address) { return Genesis::M68KReadMemoryBYTE(address); }
extern "C" unsigned int m68k_read_memory_16(unsigned int address) { return Genesis::M68KReadMemoryWORD(address); }
extern "C" unsigned int m68k_read_memory_32(unsigned int address) { return Genesis::M68KReadMemoryLONG(address); }
extern "C" unsigned int m68k_read_disassembler_8(unsigned int address) { return m68k_read_memory_8(address); }
extern "C" unsigned int m68k_read_disassembler_16(unsigned int address) { return m68k_read_memory_16(address); }
extern "C" unsigned int m68k_read_disassembler_32(unsigned int address) { return m68k_read_memory_32(address); }
extern "C" void m68k_write_memory_8(unsigned int address, unsigned int value) { Genesis::M68KWriteMemoryBYTE(address, byte(value)); }
extern "C" void m68k_write_memory_16(unsigned int address, unsigned int value) { Genesis::M68KWriteMemoryWORD(address, word(value)); }
extern "C" void m68k_write_memory_32(unsigned int address, unsigned int value) { Genesis::M68KWriteMemoryLONG(address, value); }


namespace
{
struct BcdInput
{
    unsigned kind;
    unsigned reg;
    unsigned source;
    unsigned destination;
    unsigned sr;
    unsigned address;
    bool memory;
};

// Capture only inputs. Expected outputs below follow the decimal-correction
// network independently checked against all 7,500 pinned MAME BCD vectors.
BcdInput CaptureBcd(unsigned opcode, unsigned pc)
{
    BcdInput input = {};
    const unsigned mode = (opcode >> 3) & 7;
    const unsigned reg = opcode & 7;
    if ((opcode & 0xf1f0) == 0xc100) input.kind = 1;
    else if ((opcode & 0xf1f0) == 0x8100) input.kind = 2;
    else if ((opcode & 0xffc0) == 0x4800 && mode != 1 && (mode != 7 || reg < 2)) input.kind = 3;
    if (!input.kind) return input;
    input.sr = m68k_get_reg(0, M68K_REG_SR);
    auto data = [](unsigned r) { return m68k_get_reg(0, m68k_register_t(M68K_REG_D0 + r)); };
    auto address = [](unsigned r) { return m68k_get_reg(0, m68k_register_t(M68K_REG_A0 + r)); };
    if (input.kind != 3)
    {
        input.reg = (opcode >> 9) & 7;
        input.memory = (opcode & 8) != 0;
        if (input.memory)
        {
            const unsigned sourceAddress = address(reg) - (reg == 7 ? 2 : 1);
            input.address = address(input.reg) - (input.reg == 7 ? 2 : 1);
            if (reg == input.reg) input.address -= input.reg == 7 ? 2 : 1;
            input.source = m68k_read_memory_8(sourceAddress);
            input.destination = m68k_read_memory_8(input.address);
        }
        else { input.source = data(reg) & 255; input.destination = data(input.reg) & 255; }
    }
    else
    {
        input.reg = reg;
        input.memory = mode != 0;
        unsigned effective = address(reg);
        if (mode == 4) effective -= reg == 7 ? 2 : 1;
        if (mode == 5) effective += static_cast<signed_word>(m68k_read_memory_16(pc + 2));
        if (mode == 6)
        {
            const unsigned extension = m68k_read_memory_16(pc + 2);
            const unsigned indexReg = (extension >> 12) & 7;
            unsigned index = (extension & 0x8000) ? address(indexReg) : data(indexReg);
            if (!(extension & 0x800)) index = static_cast<signed_dword>(static_cast<signed_word>(index));
            effective += index + static_cast<signed_byte>(extension);
        }
        if (mode == 7) effective = reg ? m68k_read_memory_32(pc + 2)
                                     : static_cast<signed_dword>(static_cast<signed_word>(m68k_read_memory_16(pc + 2)));
        input.address = effective;
        input.source = input.memory ? m68k_read_memory_8(effective) : data(reg) & 255;
    }
    input.address &= 0xffffff;
    return input;
}

void CorrectBcd(const BcdInput& input, bool traceBoundary)
{
    if (!input.kind) return;
    const int extend = (input.sr >> 4) & 1;
    int result;
    bool carry;
    if (input.kind == 1)
    {
        result = input.destination + input.source + extend;
        if ((input.destination & 15) + (input.source & 15) + extend > 9) result += 6;
        carry = result >= 0xa0;
        if (carry) result += 0x60;
    }
    else
    {
        const int raw = int(input.destination) - int(input.source) - extend;
        result = raw;
        if (int(input.destination & 15) - int(input.source & 15) - extend < 0) result -= 6;
        carry = result < 0;
        if (raw < 0) result -= 0x60;
    }
    const unsigned value = result & 255;
    const unsigned low = (carry ? 0x11 : 0) | (value == 0 && (input.sr & 4) ? 4 : 0);
    const bool trace = traceBoundary && (input.sr & 0x8000);
    const unsigned stack = m68k_get_reg(0, M68K_REG_ISP) & 0xffffff;
    if (input.memory)
    {
        // A following trace frame wins if it overwrote the data destination.
        if (!trace || ((input.address - stack) & 0xffffff) >= 6)
            m68k_write_memory_8(input.address, value);
    }
    else
    {
        const m68k_register_t reg = m68k_register_t(M68K_REG_D0 + input.reg);
        m68k_set_reg(reg, (m68k_get_reg(0, reg) & ~255u) | value);
    }
    m68k_set_reg(M68K_REG_SR, (m68k_get_reg(0, M68K_REG_SR) & ~0x15u) | low);
    if (trace) m68k_write_memory_16(stack, (m68k_read_memory_16(stack) & ~0x15u) | low);
}
}

void M68k::Init()
{
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_pulse_reset();
    m68k_execute(0); // Consume reset latency before the independently seeded state.
}

void M68k::LoadVectorState(const CPU_VECTOR_STATE& state)
{
    m68k_set_reg(M68K_REG_SR, state.cpu.CCR);
    for (int i = 0; i < 8; ++i) m68k_set_reg(m68k_register_t(M68K_REG_D0 + i), state.cpu.registerData[i]);
    for (int i = 0; i < 7; ++i) m68k_set_reg(m68k_register_t(M68K_REG_A0 + i), state.cpu.registerAddress[i]);
    m68k_set_reg(M68K_REG_USP, state.usp);
    m68k_set_reg(M68K_REG_ISP, state.ssp);
    m68k_set_reg(M68K_REG_PC, state.cpu.programCounter);
}

CPU_VECTOR_STATE M68k::ReadVectorState()
{
    CPU_VECTOR_STATE state = {};
    state.cpu.CCR = word(m68k_get_reg(0, M68K_REG_SR));
    state.cpu.programCounter = m68k_get_reg(0, M68K_REG_PC);
    state.cpu.stop = drivonix_reference_stopped() != 0;
    for (int i = 0; i < 8; ++i) state.cpu.registerData[i] = m68k_get_reg(0, m68k_register_t(M68K_REG_D0 + i));
    for (int i = 0; i < 8; ++i) state.cpu.registerAddress[i] = m68k_get_reg(0, m68k_register_t(M68K_REG_A0 + i));
    state.usp = m68k_get_reg(0, M68K_REG_USP);
    state.ssp = m68k_get_reg(0, M68K_REG_ISP);
    return state;
}


namespace
{
unsigned SnapshotOperand(const CPU_VECTOR_STATE& state, unsigned mode, unsigned reg, unsigned size)
{
    if (mode == 0) return state.cpu.registerData[reg];
    if (mode == 1) return state.cpu.registerAddress[reg];
    const unsigned cursor = state.cpu.programCounter + 2;
    const unsigned extension = m68k_read_memory_16(cursor);
    unsigned address = state.cpu.registerAddress[reg];
    if (mode == 4) address -= size == 0 && reg == 7 ? 2 : 1u << size;
    if (mode == 5) address += static_cast<signed_word>(extension);
    if (mode == 6 || (mode == 7 && reg == 3))
    {
        const unsigned indexReg = (extension >> 12) & 7;
        unsigned index = (extension & 0x8000) ? state.cpu.registerAddress[indexReg] : state.cpu.registerData[indexReg];
        if (!(extension & 0x800)) index = static_cast<signed_dword>(static_cast<signed_word>(index));
        address = (mode == 7 ? cursor : address) + index + static_cast<signed_byte>(extension);
    }
    if (mode == 7)
    {
        if (reg == 0) address = static_cast<signed_dword>(static_cast<signed_word>(extension));
        if (reg == 1) address = m68k_read_memory_32(cursor);
        if (reg == 2) address = cursor + static_cast<signed_word>(extension);
        if (reg == 4) address = cursor + (size == 0 ? 1 : 0);
    }
    return size == 0 ? m68k_read_memory_8(address) : size == 1 ? m68k_read_memory_16(address) : m68k_read_memory_32(address);
}

void RestoreAddressRegister(const CPU_VECTOR_STATE& before, unsigned reg, unsigned value)
{
    const m68k_register_t which = reg == 7 ? ((before.cpu.CCR & 0x2000) ? M68K_REG_ISP : M68K_REG_USP)
                                         : m68k_register_t(M68K_REG_A0 + reg);
    m68k_set_reg(which, value);
}

unsigned ExtensionBytes(unsigned mode, unsigned reg, unsigned size)
{
    if (mode == 5 || mode == 6) return 2;
    if (mode != 7) return 0;
    if (reg == 1) return 4;
    if (reg == 4) return size == 2 ? 4 : 2;
    return 2;
}

unsigned OperandAccessCycles(unsigned mode, unsigned reg, unsigned size)
{
    if (mode < 2) return 0;
    const unsigned wordCycles = size == 2 ? 8 : 4;
    if (mode == 2 || mode == 3) return wordCycles;
    if (mode == 4) return wordCycles + 2;
    if (mode == 5 || (mode == 7 && (reg == 0 || reg == 2))) return wordCycles + 4;
    if (mode == 6 || (mode == 7 && reg == 3)) return wordCycles + 6;
    if (mode == 7 && reg == 1) return wordCycles + 8;
    return wordCycles;
}

unsigned AddressCalculationCycles(unsigned mode, unsigned reg)
{
    if (mode == 4) return 2;
    if (mode == 5 || (mode == 7 && (reg == 0 || reg == 2))) return 4;
    if (mode == 6 || (mode == 7 && reg == 3)) return 6;
    if (mode == 7 && reg == 1) return 8;
    return 0;
}

// The pinned oracle models address-fault frames but assigns every operand fault
// 50 cycles and often stacks a later PC. This bounded correction follows the
// independent MAME bus sequences and Motorola's seven-word frame format.
int CorrectFault(const CPU_VECTOR_STATE& before, unsigned opcode, int cycles, const unsigned* jsrStack, unsigned moveSource)
{
    unsigned pc = m68k_get_reg(0, M68K_REG_PC);
    const unsigned top = opcode >> 12;
    unsigned mode = (opcode >> 3) & 7;
    unsigned reg = opcode & 7;
    if (pc & 1)
    {
        unsigned faultCycles = 0;
        unsigned savedPC = before.cpu.programCounter + 2;
        if (top == 6)
        {
            if (((opcode >> 8) & 15) == 1) { faultCycles = 68; savedPC = pc; }
            else faultCycles = 60;
        }
        else if ((opcode & 0xf0f8) == 0x50c8)
        {
            faultCycles = 60;
            savedPC += 2;
            m68k_set_reg(m68k_register_t(M68K_REG_D0 + reg), before.cpu.registerData[reg]);
        }
        else if ((opcode & 0xff80) == 0x4e80)
        {
            // Control-target fetches overlap part of extension processing.
            faultCycles = mode == 2 ? 58 : (mode == 6 || (mode == 7 && reg == 3)) ? 64 :
                (mode == 7 && reg == 1) ? 62 : 60;
            if ((opcode & 0xffc0) == 0x4e80)
            {
                savedPC += ExtensionBytes(mode, reg, 2);
                // JSR does its target fetch before committing the return push.
                const unsigned originalSP = before.cpu.registerAddress[7];
                m68k_set_reg((before.cpu.CCR & 0x2000) ? M68K_REG_ISP : M68K_REG_USP, originalSP);
                for (unsigned i = 0; i < 4; ++i) m68k_write_memory_8(originalSP - 4 + i, jsrStack[i]);
            }
        }
        else if (opcode == 0x4e75) faultCycles = 66;
        else if (opcode == 0x4e73 || opcode == 0x4e77) faultCycles = 70;
        if (faultCycles)
        {
            drivonix_reference_program_fault(pc, savedPC, opcode);
            return faultCycles;
        }
    }
    if (cycles != 50 || pc != m68k_read_memory_32(12)) return cycles;
    const unsigned stack = m68k_get_reg(0, M68K_REG_ISP);
    bool write = (m68k_read_memory_16(stack) & 16) == 0;
    if ((opcode & 0xff00) == 0x4200)
    {
        // A 68000 CLR memory operand performs its dummy read before flags and
        // the write. The pinned oracle omits that read, faulting too late.
        write = false;
        m68k_write_memory_16(stack, m68k_read_memory_16(stack) | 16);
        m68k_write_memory_16(stack + 8, before.cpu.CCR);
        m68k_set_reg(M68K_REG_SR, (m68k_get_reg(0, M68K_REG_SR) & ~31u) | (before.cpu.CCR & 31));
    }
    unsigned size = (opcode >> 6) & 3;
    // MOVE SR,<ea> performs a word dummy read on the original 68000, even
    // though this oracle emits its write directly. Preserve the independently
    // executed EA update and correct the resulting read-fault frame.
    if ((opcode & 0xffc0) == 0x40c0)
    {
        size = 1;
        m68k_write_memory_16(stack, m68k_read_memory_16(stack) | 16);
    }
    unsigned prefix = 0;
    if (top >= 1 && top <= 3) size = top == 2 ? 2 : top == 3 ? 1 : 0;
    else if ((top == 8 || top == 12) && ((opcode >> 6) & 3) == 3) size = 1;
    else if ((opcode & 0xf1c0) == 0x4180 || (top == 14 && size == 3)) size = 1;
    if (top == 0 && (opcode & 0x0100) == 0 && (opcode & 0xff00) != 0x0800)
        prefix = size == 2 ? 4 : 2;
    const bool movem = (opcode & 0xfb80) == 0x4880;
    if (movem) { prefix = 2; size = (opcode & 0x40) ? 2 : 1; }
    unsigned savedPC = before.cpu.programCounter + 2 + prefix;
    unsigned corrected = 58 + prefix * 2 + AddressCalculationCycles(mode, reg);
    if (top >= 1 && top <= 3 && write)
    {
        const unsigned sourceCycles = OperandAccessCycles(mode, reg, size);
        const unsigned sourceExtension = ExtensionBytes(mode, reg, size);
        mode = (opcode >> 6) & 7;
        reg = (opcode >> 9) & 7;
        corrected = 58 + sourceCycles + AddressCalculationCycles(mode, reg) + (mode == 4 ? 2 : 0);
        savedPC = before.cpu.programCounter + 2 + sourceExtension + ((mode == 7 && reg == 1) ? 4 : 2);
        const unsigned sourceMode = (opcode >> 3) & 7;
        const unsigned sourceReg = opcode & 7;
        unsigned destinationBase = before.cpu.registerAddress[reg];
        if (sourceReg == reg && (sourceMode == 3 || sourceMode == 4))
            destinationBase += sourceMode == 3 ? (1u << size) : -(1u << size);
        if (mode == 3 || (mode == 4 && size == 2)) RestoreAddressRegister(before, reg, destinationBase);
        if (mode == 4 && size == 2)
            m68k_write_memory_32(stack + 2, m68k_read_memory_32(stack + 2) + 2);
        if (mode == 4 && size == 1)
            m68k_write_memory_16(stack + 6, m68k_read_memory_16(before.cpu.programCounter + 2 + sourceExtension));
        unsigned status = before.cpu.CCR;
        const unsigned mask = size == 2 ? 0xffffffffu : 0xffffu;
        const unsigned value = moveSource & mask;
        status = (status & ~15u) | ((value & (size == 2 ? 0x80000000u : 0x8000u)) ? 8 : 0) | (value == 0 ? 4 : 0);
        const bool directSource = sourceMode <= 1 || (sourceMode == 7 && sourceReg == 4);
        if (size == 2 && (mode == 2 || mode == 3))
            status = directSource ? before.cpu.CCR : ((status & ~8u) | ((moveSource & 0x8000) ? 8 : 0));
        else if (size == 2 && mode == 7 && reg == 1 && !directSource)
            status = (status & ~8u) | ((moveSource & 0x8000) ? 8 : 0);
        else if (size == 2 && directSource && (mode == 5 || mode == 6))
            status = (status & ~3u) | (before.cpu.CCR & 3);
        m68k_write_memory_16(stack + 8, status);
        m68k_set_reg(M68K_REG_SR, (m68k_get_reg(0, M68K_REG_SR) & ~31u) | (status & 31));
    }
    else if (movem)
        savedPC += ExtensionBytes(mode, reg, size) + 2;
    else if (mode == 4 && size == 1) savedPC += 2;
    else if (mode == 7 && reg == 0) savedPC += 2;
    else if (mode == 7 && reg == 1) savedPC += 4;
    if (top >= 1 && top <= 3 && !write && mode == 3)
        RestoreAddressRegister(before, reg, before.cpu.registerAddress[reg]);
    const bool pairedExtend = ((opcode & 0xf130) == 0xd100 || (opcode & 0xf130) == 0x9100) && (opcode & 8);
    const bool compareMemory = (opcode & 0xf138) == 0xb108;
    if (pairedExtend)
    {
        const unsigned source = opcode & 7;
        const unsigned destination = (opcode >> 9) & 7;
        const bool sourceFault = (before.cpu.registerAddress[source] & 1) != 0;
        savedPC = before.cpu.programCounter + 4;
        corrected = sourceFault ? 60 : (size == 2 ? 68 : 64);
        if (size == 2)
        {
            const unsigned faultReg = sourceFault ? source : destination;
            RestoreAddressRegister(before, faultReg, before.cpu.registerAddress[faultReg]);
            m68k_write_memory_32(stack + 2, before.cpu.registerAddress[faultReg] - 2);
        }
    }
    else if (compareMemory)
    {
        const unsigned source = opcode & 7;
        const unsigned destination = (opcode >> 9) & 7;
        const bool sourceFault = (before.cpu.registerAddress[source] & 1) != 0;
        savedPC = before.cpu.programCounter + 4;
        corrected = sourceFault ? 58 : (size == 2 ? 66 : 62);
        if (sourceFault) RestoreAddressRegister(before, source, before.cpu.registerAddress[source] + 2);
        else RestoreAddressRegister(before, destination, before.cpu.registerAddress[destination]);
    }
    else if (size == 2 && mode == 3)
        RestoreAddressRegister(before, reg, before.cpu.registerAddress[reg]);
    m68k_write_memory_32(stack + 10, savedPC);
    return corrected;
}
}

namespace
{
int CorrectControlReference(const CPU_VECTOR_STATE& before, unsigned opcode, int cycles, bool traceBoundary, unsigned bounds)
{
    // The oracle decrements A7 before capturing LINK A7's old frame pointer.
    // Hardware stores the original A7, as all 2,500 pinned LINK vectors confirm.
    if (opcode == 0x4e57 && !(before.cpu.registerAddress[7] & 1) && cycles != 50)
    {
        const unsigned original = before.cpu.registerAddress[7];
        const unsigned traceStack = m68k_get_reg(0, M68K_REG_ISP);
        for (unsigned i = 0; i < 4; ++i)
        {
            const unsigned address = original - 4 + i;
            if (!(traceBoundary && (before.cpu.CCR & 0x8000) && ((address - traceStack) & 0xffffff) < 6))
                m68k_write_memory_8(address, original >> (24 - i * 8));
        }
    }
    // Pinned CHK trap entries discard EA-read timing and use one fixed trap
    // path. Independent MAME bus vectors distinguish the two signed checks.
    if ((opcode & 0xf1c0) == 0x4180 && cycles == 40 &&
        m68k_get_reg(0, M68K_REG_PC) == m68k_read_memory_32(6 * 4))
    {
        const int value = static_cast<signed_word>(before.cpu.registerData[(opcode >> 9) & 7]);
        const int bound = static_cast<signed_word>(bounds);
        const bool negativePath = value < 0 && static_cast<signed_word>(value - bound) <= 0;
        return (negativePath ? 40 : 38) + OperandAccessCycles((opcode >> 3) & 7, opcode & 7, 1);
    }
    return cycles;
}
}

int M68k::StepVector(bool, bool traceBoundary)
{
    const unsigned pc = m68k_get_reg(0, M68K_REG_PC);
    const unsigned opcode = m68k_read_memory_16(pc);
    const CPU_VECTOR_STATE before = ReadVectorState();
    const unsigned chkBounds = (opcode & 0xf1c0) == 0x4180 ? SnapshotOperand(before, (opcode >> 3) & 7, opcode & 7, 1) : 0;
    const unsigned top = opcode >> 12;
    const unsigned moveSource = top >= 1 && top <= 3 ? SnapshotOperand(before, (opcode >> 3) & 7, opcode & 7, top == 2 ? 2 : top == 3 ? 1 : 0) : 0;
    unsigned jsrStack[4] = {};
    if ((opcode & 0xffc0) == 0x4e80)
        for (unsigned i = 0; i < 4; ++i) jsrStack[i] = m68k_read_memory_8(before.cpu.registerAddress[7] - 4 + i);
    const bool dynamicBit = (opcode & 0xf100) == 0x0100;
    const bool staticBit = (opcode & 0xff00) == 0x0800;
    const unsigned bitOperation = (opcode >> 6) & 3;
    const unsigned bitIndex = dynamicBit ? m68k_get_reg(0, m68k_register_t(M68K_REG_D0 + ((opcode >> 9) & 7))) & 31
                                        : (staticBit ? m68k_read_memory_16(pc + 2) & 31 : 0);
    const BcdInput bcd = CaptureBcd(opcode, pc);
    int cycles = drivonix_reference_step(traceBoundary ? 1 : 0);
    CorrectBcd(bcd, traceBoundary);
    cycles = CorrectFault(before, opcode, cycles, jsrStack, moveSource);
    cycles = CorrectControlReference(before, opcode, cycles, traceBoundary, chkBounds);
    // DIV clears C on the divide-zero path as well as quotient overflow.
    if ((opcode & 0xf0c0) == 0x80c0 && m68k_get_reg(0, M68K_REG_PC) == m68k_read_memory_32(20))
    {
        const unsigned stack = m68k_get_reg(0, M68K_REG_ISP);
        m68k_write_memory_16(stack, m68k_read_memory_16(stack) & ~1u);
        m68k_set_reg(M68K_REG_SR, m68k_get_reg(0, M68K_REG_SR) & ~1u);
    }

    const unsigned family = opcode >> 12;
    const unsigned opmode = (opcode >> 6) & 7;
    const unsigned mode = (opcode >> 3) & 7;
    const unsigned reg = opcode & 7;
    // Pinned Musashi's 68000 cycle tables contain several 68020-style register
    // ALU entries and two-cycle immediate-source overcounts. Motorola timing
    // tables and the independently generated MAME corpus agree on these totals.
    const bool arithmetic = family == 9 || family == 13;
    const bool logical = family == 8 || family == 12;
    if (opmode == 2 && ((arithmetic && mode <= 1) || (logical && mode == 0))) cycles += 2;
    if (arithmetic && opmode == 7 && mode <= 1) cycles += 2;
    if ((arithmetic || logical) && opmode <= 1 && mode == 7 && reg == 4) cycles -= 2;
    if (arithmetic && opmode == 3 && mode == 7 && reg == 4) cycles -= 2;
    if ((opcode & 0xf1f8) == 0x5048) cycles += 4; // ADDQ.W #n,An is always 8.
    if ((opcode & 0xfff8) == 0x0280) cycles += 2; // ANDI.L #imm,Dn is 16, not 14.
    if ((dynamicBit || staticBit) && mode == 0 && bitOperation != 0 && bitIndex < 16) cycles -= 2;
    if (dynamicBit && bitOperation == 0 && mode == 7 && reg == 4) cycles += 2;
    return cycles;
}
void M68k::VectorIRQ(unsigned level) { m68k_set_irq(level); }
void M68k::VectorReset() { m68k_pulse_reset(); m68k_execute(0); }
int ReferenceOpcodeValid(unsigned opcode) { return m68k_is_valid_instruction(opcode, M68K_CPU_TYPE_68000); }
