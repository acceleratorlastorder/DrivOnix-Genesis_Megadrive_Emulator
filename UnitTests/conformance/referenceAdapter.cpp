// Test-only independent oracle. Never linked into the emulator or cpu_under_test.
#include "../../M68k/m68k.hpp"
#include "../cpuTestMemory.hpp"

extern "C" {
#include "m68k.h"
int drivonix_reference_stopped();
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

int M68k::StepVector(bool) { return m68k_execute(1); }
void M68k::VectorIRQ(unsigned level) { m68k_set_irq(level); }
void M68k::VectorReset() { m68k_pulse_reset(); m68k_execute(0); }
int ReferenceOpcodeValid(unsigned opcode) { return m68k_is_valid_instruction(opcode, M68K_CPU_TYPE_68000); }
