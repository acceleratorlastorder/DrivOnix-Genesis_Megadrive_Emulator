#include "unitTests.hpp"
 ////////////////////////////////////////
//UTILS
////////////////////////////////////////
bool TestFlag(word CCR, int C, int V, int Z, int N, int X) {
    int res = 0;

    res += (C == BitGetVal(CCR, 0));
    res += (V == BitGetVal(CCR, 1));
    res += (Z == BitGetVal(CCR, 2));
    res += (N == BitGetVal(CCR, 3));
    res += (X == BitGetVal(CCR, 4));

    if (res == 5) {
        return true;
    }

    return false;
}

////////////////////////////////////////
//OPCODE TESTS
////////////////////////////////////////
bool Test_ABCD() {
    std::cout << "Start Test_ABCD()" << std::endl;
    const std::string testName = "ABCD";
    bool testResult = true;

    CPU_STATE_DEBUG state = {};

    //Test BCD Operation with X Reset
    state.CCR = 0x0000;
    state.registerData[4] = 0x4;
    state.registerData[6] = 0x9;

    M68k::SetCpuState(state);

    word opcode = 0xCD04;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);

    state = M68k::GetCpuState();

    if (state.registerData[6] == 0x13) {
        std::cout << "\t\tTest BCD Operation with X Reset Passed" << std::endl;
    } else {
        std::cout << "\t\tTest BCD Operation with X Reset Failed" << std::endl;
        testResult = false;
    }

    //Test BCD Operation with X Set
    state.CCR = 0x0000;
    BitSet(state.CCR, X_FLAG);
    state.registerData[4] = 0x4;
    state.registerData[6] = 0x9;

    M68k::SetCpuState(state);

    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);

    state = M68k::GetCpuState();

    if (state.registerData[6] == 0x14) {
        std::cout << "\t\tTest BCD Operation with X Set Passed" << std::endl;
    } else {
        std::cout << "\t\tTest BCD Operation with X Set Failed" << std::endl;
        testResult = false;
    }

    //Test C_FLAG and X_FLAG
    state.CCR = 0x0000;
    state.registerData[4] = 0x99;
    state.registerData[6] = 0x1;

    M68k::SetCpuState(state);

    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);

    state = M68k::GetCpuState();

    //et on test
    if (TestFlag(state.CCR, 1, 0, 0, 0, 1)) {
        std::cout << "\t\tTest C_FLAG and X_FLAG Passed" << std::endl;
    } else {
        std::cout << "\t\tTest C_FLAG and X_FLAG Failed" << std::endl;
        testResult = false;
    }

    //Test Z_FLAG
    state.CCR = 0x0000;
    BitSet(state.CCR, Z_FLAG);
    state.registerData[4] = 0x2;
    state.registerData[6] = 0x1;

    M68k::SetCpuState(state);

    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);

    state = M68k::GetCpuState();

    //et on test
    if (TestFlag(state.CCR, 0, 0, 0, 0, 0)) {
        std::cout << "\t\tTest Z_FLAG Passed" << std::endl;
    } else {
        std::cout << "\t\tTest Z_FLAG Failed" << std::endl;
        testResult = false;
    }

    //indique la fin du test
    std::cout << "End Test_ABCD()" << std::endl;

    return testResult;
}

bool Test_ADD() {
    /*	EMPTY SIGNATURE
     *  1101 (register)000 (opmode)000 ((effective address)(mode)000 (register)000)
     */
    //Indique le debut du test
    std::cout << "Start Test_ADD()" << std::endl;
    const std::string testName = "ADD";
    CPU_STATE_DEBUG state = {};
    bool testResult = true;
    //Test NZVC_FLAG
    state.CCR = 0x0000;
    BitSet(state.CCR, N_FLAG);
    BitSet(state.CCR, Z_FLAG);
    BitSet(state.CCR, V_FLAG);
    BitSet(state.CCR, C_FLAG);

    dword value_1 = 0x3;
    dword value_2 = 0x6;

    state.registerData[7] = value_1;
    state.registerAddress[6] = 0xE00000;
    Genesis::M68KWriteMemoryBYTE(state.registerAddress[6], value_2);

    M68k::SetCpuState(state);

    /*	USED OPCODE
     *  1101 (register)111(7) (opmode)100 ((effective address)(mode)010 (register)110(6))
     *  binary: 1101111100010110
     *  hex: DF16
     */
    word opcode = 0xDF16;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    //execute the opcode
    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    dword result = Genesis::M68KReadMemoryBYTE(state.registerAddress[6]);
    if ((value_1 + value_2) == result && TestFlag(state.CCR, 0, 0, 0, 0, 0)) {
        std::cout << "\t\tTest normal byte add Operation with V Z N C Passed" << std::endl;
    } else {
        testResult = false;
        std::cout << "\t\tTest normal byte add Operation Failed" << std::endl;
        if (!TestFlag(state.CCR, 0, 0, 0, 0, 0)) {
            std::cout << "\t\tclear flag V Z N C failed !" << std::endl;
        }
        if (result != (value_1 + value_2)) {
            std::cout << "\t\toperation result is false result: " << result << " should be: " << (value_1 + value_2) << std::endl;
        }
    }

    /*************
     *TEST V & N FLAG*
     ************/
    state.CCR = 0x0000;

    value_1 = 0x78;
    value_2 = 0xA;

    state.registerData[7] = value_1;
    state.registerAddress[6] = 0xE00000;
    Genesis::M68KWriteMemoryBYTE(state.registerAddress[6], value_2);

    M68k::SetCpuState(state);

    /*	USED OPCODE
     *  1101 (register)111(7) (opmode)100 ((effective address)(mode)010 (register)110(6))
     *  binary: 1101111100010110
     *  hex: DF16
     */
    opcode = 0xDF16;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    //execute the opcode
    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    result = Genesis::M68KReadMemoryBYTE(state.registerAddress[6]);
    if ((value_1 + value_2) == result && TestFlag(state.CCR, 0, 1, 0, 1, 0)) {
        std::cout << "\t\tTest normal byte add Operation with V flag Passed" << std::endl;
    } else {
        testResult = false;
        std::cout << "\t\tTest normal byte add Operation Failed" << std::endl;
        if (!TestFlag(state.CCR, 0, 1, 0, 1, 0)) {
            std::cout << "\t\traise V flag failed !" << std::endl;
        }
        if (result != (value_1 + value_2)) {
            std::cout << "\t\toperation result is false result: " << result << " should be: " << (value_1 + value_2) << std::endl;
        }
    }

    /*
    BitSet(state.CCR, X_FLAG);
    BitSet(state.CCR, N_FLAG);
    BitSet(state.CCR, Z_FLAG);
    BitSet(state.CCR, V_FLAG);
    BitSet(state.CCR, C_FLAG);
    */

    //indique la fin du test
    std::cout << "End Test_ADD()" << std::endl;
    return testResult;
}

////////////////////////////////////////
//OPCODE TESTS
////////////////////////////////////////
bool Test_ADDA() {
    // ADDA always writes an address register and never changes the status flags.
    struct Case { word opcode; dword data; dword address; dword otherAddress; dword expected; };
    const Case cases[] = {
        {0xD1C0, 1, 2, 0, 3},             // ADDA.L D0,A0
        {0xD0C9, 0, 2, 1, 3},             // ADDA.W A1,A0
        {0xD0C0, 0xFFFF, 0x10000, 0, 0xFFFF}, // word source is sign extended
        {0xD1C0, 1, 0xFFFFFFFF, 0, 0}      // 32-bit address arithmetic wraps
    };
    bool ok = true;
    for (const Case& test : cases) {
        CPU_STATE_DEBUG state = {};
        state.CCR = 0x271F;
        state.registerData[0] = test.data;
        state.registerAddress[0] = test.address;
        state.registerAddress[1] = test.otherAddress;
        M68k::SetCpuState(state);
        M68k::ExecuteOpcode(test.opcode);
        state = M68k::GetCpuState();
        if (state.registerAddress[0] != test.expected ||
            state.registerData[0] != test.data ||
            state.registerAddress[1] != test.otherAddress || state.CCR != 0x271F) {
            std::cerr << "ADDA failed for opcode 0x" << std::hex << test.opcode << std::endl;
            ok = false;
        }
    }
    return ok;
}


bool Test_ASL_ASR_TO_MEMORY() {
    struct Case { word opcode; word operand; word expected; word status; };
    const Case cases[] = {
        {0xE0D6, 0xFFFF, 0xFFFF, 0x2719}, // ASR.W (A6), negative sign fill
        {0xE0D6, 3, 1, 0x2711},
        {0xE1D6, 1, 2, 0x2700}           // ASL.W (A6)
    };
    bool ok = true;
    for (const Case& test : cases) {
        CPU_STATE_DEBUG state = {};
        state.CCR = 0x271F;
        state.registerAddress[6] = 0xE00020;
        Genesis::M68KWriteMemoryWORD(0xE00020, test.operand);
        M68k::SetCpuState(state);
        M68k::ExecuteOpcode(test.opcode);
        state = M68k::GetCpuState();
        if (Genesis::M68KReadMemoryWORD(0xE00020) != test.expected ||
            state.CCR != test.status || state.registerAddress[6] != 0xE00020) {
            std::cerr << "ASL/ASR memory test failed" << std::endl;
            ok = false;
        }
    }
    return ok;
}
bool Test_ASL_ASR_TO_REGISTER() {
    // A zero register count preserves the operand/X, clears C/V, and updates N/Z.
    const word opcodes[] = {0xE4A6, 0xE5A6}; // ASR.L/ASL.L D2,D6
    const dword operands[] = {0x80000001, 0, 1};
    const word expected[] = {0x2718, 0x2714, 0x2710};
    bool ok = true;
    for (word opcode : opcodes) {
        for (int i = 0; i < 3; ++i) {
            CPU_STATE_DEBUG state = {};
            state.CCR = 0x271F;
            state.registerData[6] = operands[i];
            M68k::SetCpuState(state);
            M68k::ExecuteOpcode(opcode);
            state = M68k::GetCpuState();
            if (state.registerData[6] != operands[i] || state.CCR != expected[i]) {
                std::cerr << "ASL/ASR zero-count register test failed" << std::endl;
                ok = false;
            }
        }
    }
    return ok;
}


bool Test_SBCD() {
    const std::string testName = "SBCD";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state = {};
    state.registerData[3] = 0x54;
    state.registerData[2] = 0x5;
    BitReset(state.CCR, X_FLAG);
    BitSet(state.CCR, Z_FLAG);

    M68k::SetCpuState(state);

    word opcode = 0x8702; //dest = d3 > src = d2 > rm = 0
    std::cout << "\t\texecute\n" << testName << " with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    if (state.registerData[3] == 0x49) {
        std::cout << "\t\tSub Test Passed" << std::endl;
    } else {
        std::cout << "\t\tSub Test Failed -> Expected : 0x49 - Result : 0x" << std::hex << state.registerData[3] << std::endl;
        testResult = false;
    }

    if (TestFlag(state.CCR, 0, 0, 0, 0, 0)) {
        std::cout << "\t\tZ_Flag Test Passed" << std::endl;
    } else {
        std::cout << "\t\tZ_Flag Test Failed" << std::endl;
        testResult = false;
    }

    state.registerData[3] = 0x54;
    state.registerData[2] = 0x5;
    BitSet(state.CCR, X_FLAG);

    M68k::SetCpuState(state);

    opcode = 0x8702; //dest = d3 > src = d2 > rm = 0
    std::cout << "\t\texecute\n" << testName << " with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    if (state.registerData[3] == 0x48) {
        std::cout << "\t\tSub X Test Passed" << std::endl;
    } else {
        std::cout << "\t\tSub X Test Failed -> Expected : 0x48 - Result : 0x" << std::hex << state.registerData[3] << std::endl;
        testResult = false;
    }

    state.registerData[3] = 0x12;
    state.registerData[2] = 0x13;
    BitReset(state.CCR, X_FLAG);
    BitReset(state.CCR, C_FLAG);

    M68k::SetCpuState(state);

    opcode = 0x8702; //dest = d3 > src = d2 > rm = 0
    std::cout << "\t\texecute\n" << testName << " with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    if (state.registerData[3] == 0x99) {
        std::cout << "\t\tSub Borrow Test Passed" << std::endl;
    } else {
        std::cout << "\t\tSub Borrow Test Failed -> Expected : 0x99 - Result : 0x" << std::hex << state.registerData[3] << std::endl;
        testResult = false;
    }

    if (TestFlag(state.CCR, 1, 0, 0, 0, 1)) {
        std::cout << "\t\tC_X_FLAG Test Passed" << std::endl;
    } else {
        std::cout << "\t\tC_X_Flag Test Failed" << std::endl;
        testResult = false;
    }

    state.registerData[3] = 0x12;
    state.registerData[2] = 0x13;
    BitSet(state.CCR, X_FLAG);
    BitReset(state.CCR, C_FLAG);

    M68k::SetCpuState(state);

    opcode = 0x8702; //dest = d3 > src = d2 > rm = 0
    std::cout << "\t\texecute\n" << testName << " with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    if (state.registerData[3] == 0x98) {
        std::cout << "\t\tSub Borrow X Test Passed" << std::endl;
    } else {
        std::cout << "\t\tSub Borrow X Test Failed -> Expected : 0x98 - Result : 0x" << std::hex << state.registerData[3] << std::endl;
        testResult = false;
    }

    if (TestFlag(state.CCR, 1, 0, 0, 0, 1)) {
        std::cout << "\t\tC_X_FLAG X Test Passed" << std::endl;
    } else {
        std::cout << "\t\tC_X_Flag X Test Failed" << std::endl;
        testResult = false;
    }

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}


bool Test_NBCD() {
    /*	EMPTY SIGNATURE
     *  0100100000 ((effective address)(mode)000 (register)000)
     *  0x4800
     */
    const std::string testName = "NBCD";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state = {};

    word opcode;
    dword result;
    dword value_1 = 0x8;
    dword expectedResult = 0x92;

    state.CCR = 0x0000;

    state.registerAddress[6] = 0xE00000;
    Genesis::M68KWriteMemoryBYTE(state.registerAddress[6], value_1);

    M68k::SetCpuState(state);
    /*	USED OPCODE
     *  0100100000 ((effective address)(mode)010("(An)") (register)110(6))
     *  binary: 0100100000010110
     *  hex: 0x4806
     */
    opcode = 0x4816;

    //execute the opcode
    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    result = Genesis::M68KReadMemoryBYTE(state.registerAddress[6]);

    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    if (result == expectedResult && TestFlag(state.CCR, 1, 0, 0, 0, 1)) {
        std::cout << "\t\tTest NBCD Operation with X Unset Passed" << std::endl;
    } else {
        testResult = false;
        std::cout << "\t\tTest NBCD Operation with X Unset Failed" << std::endl;
        if (!TestFlag(state.CCR, 1, 0, 0, 0, 1)) {
            std::cout << "\t\tflag test failed !" << std::endl;
        }
        if (result != expectedResult) {
            std::cout << "\t\toperation result is false result: " << std::uppercase << std::hex << result << " should be: " << std::uppercase << std::hex << expectedResult << std::endl;
        }
    }

    expectedResult = 0x91;
    //Test BCD Operation with X Set
    state.CCR = 0x0000;
    BitSet(state.CCR, X_FLAG);
    state.registerAddress[6] = 0xE00000;
    Genesis::M68KWriteMemoryBYTE(state.registerAddress[6], value_1);

    M68k::SetCpuState(state);

    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    //execute the opcode
    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    result = Genesis::M68KReadMemoryBYTE(state.registerAddress[6]);

    if (result == 0x91 && TestFlag(state.CCR, 1, 0, 0, 0, 1)) {
        std::cout << "\t\tTest NBCD Operation with X Set Passed" << std::endl;
    } else {
        testResult = false;
        std::cout << "\t\tTest NBCD Operation with X Set Failed" << std::endl;
        if (!TestFlag(state.CCR, 1, 0, 0, 0, 1)) {
            std::cout << "\t\tflag test failed !" << std::endl;
        }
        if (result != expectedResult) {
            std::cout << "\t\toperation result is false result: " << std::uppercase << std::hex << result << " should be: " << std::uppercase << std::hex << expectedResult << std::endl;
        }
    }

    //Test C_FLAG and X_FLAG
    state.CCR = 0x0000;
    BitSet(state.CCR, X_FLAG);
    value_1 = 0x99;
    expectedResult = 0x0;
    Genesis::M68KWriteMemoryBYTE(state.registerAddress[6], value_1);

    M68k::SetCpuState(state);

    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);

    state = M68k::GetCpuState();
    result = Genesis::M68KReadMemoryBYTE(state.registerAddress[6]);

    std::cout << "\t\tvalue_1 0x" << std::uppercase << std::hex << value_1 << std::endl;
    std::cout << "\t\tresult  0x" << std::uppercase << std::hex << result << std::endl;

    if (result == expectedResult && TestFlag(state.CCR, 1, 0, 0, 0, 1)) {
        std::cout << "\t\tTest 0x99 with X flag set Passed !" << std::endl;
    } else {
        std::cout << "\t\tTest 0x99 with X flag set Failed !" << std::endl;
        testResult = false;
        if (!TestFlag(state.CCR, 1, 0, 0, 0, 1)) {
            std::cout << "\t\tflag test failed !" << std::endl;
        }
        if (result != expectedResult) {
            std::cout << "\t\toperation result is false result: 0x" << std::uppercase << std::hex << result << " should be: 0x" << std::uppercase << std::hex << expectedResult << std::endl;
        }
    }

    //indique la fin du test
    std::cout << "End Test_" << testName << "()" << std::endl;

    return testResult;
}

////////////////////////////////////////
//MAIN
////////////////////////////////////////
int main() {
    // All 84 original groups have real specifications in conformance/specifications.py.
    // Retain these original asserted C++ cases as an additional regression layer.
    const std::map<std::string, bool (*)()> tests = {
        {"Test_ABCD", Test_ABCD},
        {"Test_ADD", Test_ADD},
        {"Test_ADDA", Test_ADDA},
        {"Test_ANDI_TO_CCR", Test_ANDI_TO_CCR},
        {"Test_ANDI_TO_SR", Test_ANDI_TO_SR},
        {"Test_ASL_ASR_TO_MEMORY", Test_ASL_ASR_TO_MEMORY},
        {"Test_ASL_ASR_TO_REGISTER", Test_ASL_ASR_TO_REGISTER},
        {"Test_DIVU", Test_DIVU},
        {"Test_EORI_TO_CCR", Test_EORI_TO_CCR},
        {"Test_EORI_TO_SR", Test_EORI_TO_SR},
        {"Test_MOVE_TO_CCR", Test_MOVE_TO_CCR},
        {"Test_MOVE_FROM_SR", Test_MOVE_FROM_SR},
        {"Test_MOVE_TO_SR", Test_MOVE_TO_SR},
        {"Test_MOVE_USP", Test_MOVE_USP},
        {"Test_MULU", Test_MULU},
        {"Test_SBCD", Test_SBCD},
        {"Test_NBCD", Test_NBCD},
    };
    Genesis::AllocM68kMemory();
    int failures = 0;
    int executed = 0;
    for (const auto& test : tests) {
        Genesis::ResetM68kMemory();
        M68k::Init();
        M68k::SetUnitTestsMode();
        ++executed;
        bool pass = test.second();
        failures += !pass;
        std::cout << (pass ? "PASS " : "FAIL ") << test.first << std::endl;
    }
    std::cout << std::dec << "Legacy suite: " << executed << " checked groups, "
              << failures << " failures; see conformance/specifications.py for all 84 groups" << std::endl;
    return failures == 0 ? 0 : 1;
}
