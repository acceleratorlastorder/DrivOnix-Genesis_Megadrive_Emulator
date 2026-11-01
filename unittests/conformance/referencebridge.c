/* Read stop state through the pinned oracle's context, never the tested CPU. */
#include "m68kcpu.h"
int drivonix_reference_stopped(void) { return (m68ki_cpu.stopped & STOP_LEVEL_STOP) != 0; }

/* A corpus instruction step stops before the hardware trace boundary. Keep SR
 * itself intact, so privilege checks and explicit SR writes remain observable. */
static int trace_boundary = 1;
static void instruction_hook(unsigned int pc)
{
    (void)pc;
    if (!trace_boundary) m68ki_tracing = 0;
}
int drivonix_reference_step(int boundary)
{
    /* Pinned Musashi incorrectly dispatches this 68040-only instruction on a
     * 68000. Route it through its own documented F-line exception machinery. */
    const unsigned int opcode = m68k_read_memory_16(REG_PC);
    if ((opcode & 0xFFF8) == 0xF620)
    {
        REG_PPC = REG_PC;
        REG_IR = opcode;
        m68ki_exception_1111();
        return 34;
    }
    trace_boundary = boundary;
    m68k_set_instr_hook_callback(instruction_hook);
    return m68k_execute(1);
}

void drivonix_reference_program_fault(unsigned target, unsigned savedPC, unsigned opcode)
{
    REG_PC = savedPC;
    REG_IR = opcode;
    m68ki_aerr_address = target;
    m68ki_aerr_write_mode = MODE_READ;
    m68ki_aerr_fc = FLAG_S ? FUNCTION_CODE_SUPERVISOR_PROGRAM : FUNCTION_CODE_USER_PROGRAM;
    CPU_INSTR_MODE = INSTRUCTION_YES;
    m68ki_exception_address_error();
}
