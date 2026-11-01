#include "unittests.hpp"

namespace
{
    bool optimizedDispatch = false;
    const dword operandAddress = 0xE00002;
    const dword supervisorStack = 0xE08000;
    const dword privilegeHandler = 0xE01000;
    const dword traceHandler = 0xE01200;

    bool Expect(const char* test, const char* field, dword actual, dword expected)
    {
        if (actual == expected) return true;
        std::cerr << test << ": " << field << " = 0x" << std::hex << actual
                  << ", expected 0x" << expected << std::dec << std::endl;
        return false;
    }

    void Prepare(CPU_STATE_DEBUG state, word immediate = 0)
    {
#ifdef DRIVONIX_CPU_TESTS
        // Seed real vector/stack memory. Privileged instructions must enter an
        // exception, including saving the old user SR and faulting instruction PC.
        const auto seedLong=[](dword address,dword value) {
            for(unsigned i=0;i<4;++i) CpuTestMemory::SeedByte(address+i,byte(value>>(24-8*i)));
        };
        seedLong(0,supervisorStack);
        seedLong(8*4,privilegeHandler);
        seedLong(9*4,traceHandler);
#endif
        M68k::Init();
        M68k::SetUnitTestsMode();
        state.programCounter = operandAddress;
        if(!state.registerAddress[7]) state.registerAddress[7]=(state.CCR&0x2000)?supervisorStack:0xE07000;
        Genesis::M68KWriteMemoryWORD(operandAddress, immediate);
        M68k::SetCpuState(state);
    }

    CPU_STATE_DEBUG Execute(word opcode)
    {
        if (optimizedDispatch)
        {
            CPU_STATE_DEBUG state = M68k::GetCpuState();
            state.programCounter -= 2;
            Genesis::M68KWriteMemoryWORD(state.programCounter, opcode);
            M68k::SetCpuState(state);
            M68k::Update();
        }
        else
        {
            M68k::ExecuteOpcode(opcode);
        }
        return M68k::GetCpuState();
    }

    bool CheckImmediate(const char* name, word opcode, word initial, word immediate, word expected,
                        bool privilegeViolation = false)
    {
        CPU_STATE_DEBUG state = {};
        state.CCR = initial;
        state.registerData[0] = 0x12345678;
        Prepare(state, immediate);
        state = Execute(opcode);
        const bool trace=optimizedDispatch && (initial&0x8000) && !privilegeViolation;
        const word savedStatus=privilegeViolation ? initial:expected;
        const word finalStatus=(privilegeViolation || trace) ? word((savedStatus|0x2000)&~0x8000):expected;
        bool ok = Expect(name, "SR", state.CCR, finalStatus);
        const dword finalPC=privilegeViolation ? privilegeHandler:(trace ? traceHandler:operandAddress+2);
        ok = Expect(name, "PC", state.programCounter, finalPC) && ok;
        if(privilegeViolation || trace) {
            ok=Expect(name,"supervisor stack",state.registerAddress[7],supervisorStack-6)&&ok;
            ok=Expect(name,"saved SR",Genesis::M68KReadMemoryWORD(supervisorStack-6),savedStatus)&&ok;
            ok=Expect(name,"saved PC",Genesis::M68KReadMemoryLONG(supervisorStack-4),privilegeViolation?operandAddress-2:operandAddress+2)&&ok;
        }
        return Expect(name, "D0 preserved", state.registerData[0], 0x12345678) && ok;
    }
}

bool Test_ANDI_TO_CCR()
{
    bool ok = CheckImmediate("ANDI to CCR", 0x023C, 0x270F, 0xFF05, 0x2705);
    return CheckImmediate("ANDI to CCR clear", 0x023C, 0x271F, 0xFFE0, 0x2700) && ok;
}

bool Test_ANDI_TO_SR()
{
    bool ok = CheckImmediate("ANDI to SR", 0x027C, 0xA71F, 0x5F0F, 0x070F);
    return CheckImmediate("ANDI to SR user mode", 0x027C, 0x0005, 0, 0x0005, true) && ok;
}

bool Test_EORI_TO_CCR()
{
    // Overlapping bits distinguish XOR from OR; reserved immediate bits are ignored.
    bool ok = CheckImmediate("EORI to CCR", 0x0A3C, 0x2705, 0xFF03, 0x2706);
    return CheckImmediate("EORI to CCR clear", 0x0A3C, 0x271F, 0xFFFF, 0x2700) && ok;
}

bool Test_EORI_TO_SR()
{
    bool ok = CheckImmediate("EORI to SR reserved bits", 0x0A7C, 0x2705, 0x00FF, 0x271A);
    ok = CheckImmediate("EORI to SR system bits", 0x0A7C, 0x2705, 0xFFFF, 0x801A) && ok;
    return CheckImmediate("EORI to SR user mode", 0x0A7C, 0x0005, 0xFFFF, 0x0005, true) && ok;
}

bool Test_MOVE_TO_CCR()
{
    bool ok = true;
    const dword operands[] = {0xABCDFFFF, 0xCAFE0000};
    const word expected[] = {0x271F, 0x2700};
    for (int i = 0; i < 2; ++i)
    {
        CPU_STATE_DEBUG state = {};
        state.CCR = 0x2715;
        state.registerData[0] = operands[i];
        Prepare(state);
        state = Execute(0x44C0);
        ok = Expect("MOVE to CCR", "SR", state.CCR, expected[i]) && ok;
        ok = Expect("MOVE to CCR", "D0 preserved", state.registerData[0], operands[i]) && ok;
        ok = Expect("MOVE to CCR", "PC", state.programCounter, operandAddress) && ok;
    }
    return ok;
}

bool Test_MOVE_FROM_SR()
{
    bool ok = true;
    // MOVE from SR is unprivileged on the Genesis's 68000 (unlike the 68010).
    const word status[] = {0x2705, 0x0005};
    for (int i = 0; i < 2; ++i)
    {
        CPU_STATE_DEBUG state = {};
        state.CCR = status[i];
        state.registerData[0] = 0xCAFEFFFF;
        Prepare(state);
        state = Execute(0x40C0);
        ok = Expect("MOVE from SR", "D0", state.registerData[0], 0xCAFE0000 | status[i]) && ok;
        ok = Expect("MOVE from SR", "SR preserved", state.CCR, status[i]) && ok;
        ok = Expect("MOVE from SR", "PC", state.programCounter, operandAddress) && ok;
    }
    return ok;
}

bool Test_MOVE_TO_SR()
{
    bool ok = true;
    const dword operands[] = {0xABCDFFFF, 0xCAFE0005};
    const word expected[] = {0xA71F, 0x0005};
    for (int i = 0; i < 2; ++i)
    {
        CPU_STATE_DEBUG state = {};
        state.CCR = 0x2700;
        state.registerData[0] = operands[i];
        Prepare(state);
        state = Execute(0x46C0);
        ok = Expect("MOVE to SR", "SR", state.CCR, expected[i]) && ok;
        ok = Expect("MOVE to SR", "D0 preserved", state.registerData[0], operands[i]) && ok;
        ok = Expect("MOVE to SR", "PC", state.programCounter, operandAddress) && ok;
    }
    CPU_STATE_DEBUG state = {};
    state.CCR = 0x0005;
    state.registerData[0] = 0x2700;
    Prepare(state);
    state = Execute(0x46C0);
    ok = Expect("MOVE to SR user mode", "exception SR", state.CCR, 0x2005) && ok;
    ok = Expect("MOVE to SR user mode", "handler PC", state.programCounter, privilegeHandler) && ok;
    ok = Expect("MOVE to SR user mode", "SSP", state.registerAddress[7], supervisorStack-6) && ok;
    ok = Expect("MOVE to SR user mode", "saved user SR", Genesis::M68KReadMemoryWORD(supervisorStack-6), 0x0005) && ok;
    ok = Expect("MOVE to SR user mode", "faulting PC", Genesis::M68KReadMemoryLONG(supervisorStack-4), operandAddress-2) && ok;
    return ok;
}

bool Test_MOVE_USP()
{
    CPU_STATE_DEBUG state = {};
    state.CCR = 0x2715;
    state.registerAddress[0] = 0x12345678;
    state.registerAddress[1] = 0xBAD00001;
    state.registerAddress[2] = 0x87654321;
    state.registerAddress[3] = 0xBAD00003;
    Prepare(state);

    Execute(0x4E60); // MOVE A0,USP
    state = Execute(0x4E69); // MOVE USP,A1
    bool ok = Expect("MOVE USP", "A1 round trip", state.registerAddress[1], 0x12345678);
    ok = Expect("MOVE USP", "A0 preserved", state.registerAddress[0], 0x12345678) && ok;

    Execute(0x4E62); // MOVE A2,USP: also verify that USP can be overwritten.
    state = Execute(0x4E6B); // MOVE USP,A3
    ok = Expect("MOVE USP", "A3 round trip", state.registerAddress[3], 0x87654321) && ok;
    ok = Expect("MOVE USP", "A2 preserved", state.registerAddress[2], 0x87654321) && ok;
    ok = Expect("MOVE USP", "SR preserved", state.CCR, 0x2715) && ok;
    return Expect("MOVE USP", "PC", state.programCounter, operandAddress) && ok;
}

bool Test_DIVU()
{
    struct Case { dword dividend; word divisor; dword result; word flags; bool overflow; };
    const Case cases[] = {
        {16, 4, 4, 0x10, false},
        {17, 4, 0x00010004, 0x10, false},
        {0, 0xFFFF, 0, 0x14, false},
        {0x8000, 1, 0x8000, 0x18, false},
        {0xFFFF, 1, 0xFFFF, 0x18, false},
        {0xFFFE0001, 0xFFFF, 0xFFFF, 0x18, false},
        {0x10000, 1, 0x10000, 0x12, true},
        {0xFFFFFFFF, 0xFFFF, 0xFFFFFFFF, 0x12, true}
    };
    bool ok = true;
    for (const Case& test : cases)
    {
        CPU_STATE_DEBUG state = {};
        state.CCR = 0x271F;
        state.registerData[0] = 0xABCD0000 | test.divisor;
        state.registerData[1] = test.dividend;
        Prepare(state);
        state = Execute(0x82C0);
        ok = Expect("DIVU", "D1 (remainder:quotient)", state.registerData[1], test.result) && ok;
        // N/Z are undefined on overflow; X is preserved and C is always cleared.
        const word mask = test.overflow ? 0x13 : 0x1F;
        ok = Expect("DIVU", "flags", state.CCR & mask, test.flags) && ok;
        ok = Expect("DIVU", "system SR", state.CCR & 0xFF00, 0x2700) && ok;
        ok = Expect("DIVU", "D0 preserved", state.registerData[0], 0xABCD0000 | test.divisor) && ok;
        ok = Expect("DIVU", "PC", state.programCounter, operandAddress) && ok;
    }
    return ok;
}

bool Test_MULU()
{
    struct Case { word left; word right; dword result; word flags; };
    const Case cases[] = {
        {4, 3, 12, 0x10},
        {0, 0xFFFF, 0, 0x14},
        {0x100, 0x100, 0x10000, 0x10},
        {0x8000, 1, 0x8000, 0x10},
        {0xFFFF, 2, 0x1FFFE, 0x10},
        {0xFFFF, 0xFFFF, 0xFFFE0001, 0x18}
    };
    bool ok = true;
    for (const Case& test : cases)
    {
        CPU_STATE_DEBUG state = {};
        state.CCR = 0x271F;
        state.registerData[0] = 0xABCD0000 | test.left;
        state.registerData[1] = 0xCAFE0000 | test.right;
        Prepare(state);
        state = Execute(0xC2C0);
        ok = Expect("MULU", "32-bit product", state.registerData[1], test.result) && ok;
        ok = Expect("MULU", "SR", state.CCR, 0x2700 | test.flags) && ok;
        ok = Expect("MULU", "D0 preserved", state.registerData[0], 0xABCD0000 | test.left) && ok;
        ok = Expect("MULU", "PC", state.programCounter, operandAddress) && ok;
    }
    return ok;
}

int RunOpcodeCoverage(bool optimized)
{
    optimizedDispatch = optimized;
    Genesis::AllocM68kMemory();
    struct Test { const char* name; bool (*run)(); };
    const Test tests[] = {
        {"ANDI_TO_CCR", Test_ANDI_TO_CCR}, {"ANDI_TO_SR", Test_ANDI_TO_SR},
        {"DIVU", Test_DIVU}, {"EORI_TO_CCR", Test_EORI_TO_CCR},
        {"EORI_TO_SR", Test_EORI_TO_SR}, {"MOVE_TO_CCR", Test_MOVE_TO_CCR},
        {"MOVE_FROM_SR", Test_MOVE_FROM_SR}, {"MOVE_TO_SR", Test_MOVE_TO_SR},
        {"MOVE_USP", Test_MOVE_USP}, {"MULU", Test_MULU}
    };
    int failures = 0;
    for (const Test& test : tests)
    {
        Genesis::ResetM68kMemory();
        const bool pass = test.run();
        failures += !pass;
        std::cout << (pass ? "PASS " : "FAIL ") << test.name << std::endl;
    }
    std::cout << (optimized ? "Optimized" : "Reference") << " dispatch: "
              << failures << "/10 test groups failed" << std::endl;
    return failures == 0 ? 0 : 1;
}
