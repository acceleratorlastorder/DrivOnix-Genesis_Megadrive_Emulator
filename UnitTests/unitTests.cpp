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

    CPU_STATE_DEBUG state;

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
    CPU_STATE_DEBUG state;
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
    std::cout << "Start Test_ADDA()" << std::endl;
    const std::string testName = "ADDA";
    bool testResult = true;

    CPU_STATE_DEBUG state;

    //Test ADDA with destination register D0 and source register D1
    state.CCR = 0x0000;
    state.registerData[0] = 0x1;
    state.registerData[1] = 0x2;

    M68k::SetCpuState(state);

    word opcode = 0xD1C0;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);

    state = M68k::GetCpuState();

    if (state.registerData[0] == 0x3) {
        std::cout << "\t\tTest ADDA with destination register D0 and source register D1 Passed" << std::endl;
    } else {
        std::cout << "\t\tTest ADDA with destination register D0 and source register D1 Failed" << std::endl;
        testResult = false;
    }

    //Test ADDA with destination register A0 and source register A1
    state.CCR = 0x0000;
    state.registerData[8] = 0x1;
    state.registerData[9] = 0x2;

    M68k::SetCpuState(state);

    opcode = 0xD0C8;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);

    state = M68k::GetCpuState();

    if (state.registerData[8] == 0x3) {
        std::cout << "\t\tTest ADDA with destination register A0 and source register A1 Passed" << std::endl;
    } else {
        std::cout << "\t\tTest ADDA with destination register A0 and source register A1 Failed" << std::endl;
        testResult = false;
    }
    //Test C_FLAG
    state.CCR = 0x0000;
    state.registerData[0] = 0x7FFF;
    state.registerData[1] = 0x1;

    M68k::SetCpuState(state);

    opcode = 0xD1C0;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);

    state = M68k::GetCpuState();

    //set the C_FLAG and test
    BitSet(state.CCR, C_FLAG);

    if (TestFlag(state.CCR, 1, 0, 0, 0, 0)) {
        std::cout << "\t\tTest C_FLAG Passed" << std::endl;
    } else {
        std::cout << "\t\tTest C_FLAG Failed" << std::endl;
        testResult = false;
    }

    //Test V_FLAG
    state.CCR = 0x0000;
    state.registerData[0] = 0x8000;
    state.registerData[1] = 0x1;

    M68k::SetCpuState(state);

    opcode = 0xD1C0;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);

    state = M68k::GetCpuState();

    //set the V_FLAG and test
    BitSet(state.CCR, V_FLAG);

    if (TestFlag(state.CCR, 0, 1, 0, 0, 0)) {
        std::cout << "\t\tTest V_FLAG Passed" << std::endl;
    } else {
        std::cout << "\t\tTest V_FLAG Failed" << std::endl;
        testResult = false;
    }

    //Test Z_FLAG
    state.CCR = 0x0000;
    state.registerData[0] = 0x0;
    state.registerData[1] = 0x0;

    M68k::SetCpuState(state);

    opcode = 0xD1C0;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);

    state = M68k::GetCpuState();

    //set the Z_FLAG and test
    BitSet(state.CCR, Z_FLAG);

    if (TestFlag(state.CCR, 0, 0, 1, 0, 0)) {
        std::cout << "\t\tTest Z_FLAG Passed" << std::endl;
    } else {
        std::cout << "\t\tTest Z_FLAG Failed" << std::endl;
        testResult = false;
    }

    //Test N_FLAG
    state.CCR = 0x0000;
    state.registerData[0] = 0x1;
    state.registerData[1] = 0x7FFF;

    M68k::SetCpuState(state);

    opcode = 0xD1C0;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);

    state = M68k::GetCpuState();

    //set the N_FLAG and test
    BitSet(state.CCR, N_FLAG);

    if (TestFlag(state.CCR, 0, 0, 0, 1, 0)) {
        std::cout << "\t\tTest N_FLAG Passed" << std::endl;
    } else {
        std::cout << "\t\tTest N_FLAG Failed" << std::endl;
        testResult = false;
    }

    //Test X_FLAG
    state.CCR = 0x0000;
    state.registerData[0] = 0x1;
    state.registerData[1] = 0x8000;

    M68k::SetCpuState(state);

    opcode = 0xD1C0;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);

    state = M68k::GetCpuState();

    //set the X_FLAG and test
    BitSet(state.CCR, X_FLAG);

    if (TestFlag(state.CCR, 0, 0, 0, 0, 1)) {
        std::cout << "\t\tTest X_FLAG Passed" << std::endl;
    } else {
        std::cout << "\t\tTest X_FLAG Failed" << std::endl;
        testResult = false;
    }

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}

bool Test_ADDI() {
    /*	EMPTY SIGNATURE
     *  00000110 (size)00 ((effective address)(mode)000 (register)000)
     *  binary: 00000110 00 000 000
     */
    const std::string testName = "ADDI";
    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    /*	USED OPCODE
     *  00000110 (size)00 ((effective address)(mode)000 (register)110(6))
     *  binary: 00000110 00 000 110
     *  hex: 0606
     */
    word opcode = 0x0606;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_ADDQ() {
    /*	EMPTY SIGNATURE
     *  0101 (data)000 0 (size)00 ((effective address)(mode)000 (register)000)
     *  binary: 0101 000 0 00 000 000
     */
    const std::string testName = "ADDQ";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    /*	USED OPCODE
     *  0101 (data)000 0 (size)00 ((effective address)(mode)001 (register)110)
     *  binary: 0101 000 0 00 001 110
     *  hex: 0606
     */
    word opcode = 0x500E;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_ADDX() {
    const std::string testName = "ADDX";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0xD101;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_AND() {
    const std::string testName = "AND";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0xC001;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_ANDI() {
    const std::string testName = "ANDI";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x201;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_ASL_ASR_TO_MEMORY() {
    const std::string testName = "Test_ASL_ASR_TO_MEMORY";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0xE4A6;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_ASL_ASR_TO_REGISTER() {
    const std::string testName = "Test_ASL_ASR_TO_REGISTER";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0xE4A6;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_Bcc() {
    const std::string testName = "Bcc";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x4816;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_BCHG() {
    const std::string testName = "BCHG";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x554;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_BCLR() {
    const std::string testName = "BCLR";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x581;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_BRA() {
    const std::string testName = "BRA";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x4816;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_BSET() {
    const std::string testName = "BSET";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x5EC;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_BSR() {
    const std::string testName = "BSR";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x6101;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_BTST() {
    const std::string testName = "BTST";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x102;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_CHK() {
    const std::string testName = "CHK";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x4816;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_CLR() {
    const std::string testName = "CLR";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x4816;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_CMP() {
    const std::string testName = "CMP";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x4816;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_CMPA() {
    const std::string testName = "CMPA";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0xBFCE;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_CMPI() {
    const std::string testName = "CMPI";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0xC01;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_CMPM() {
    const std::string testName = "CMPM";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0xB10B;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_DBcc() {
    const std::string testName = "DBcc";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x4816;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_DIVS() {
    const std::string testName = "DIVS";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x4816;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_EOR() {
    const std::string testName = "EOR";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x4816;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_EORI() {
    const std::string testName = "EORI";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x4816;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_EXG() {
    const std::string testName = "EXG";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x4816;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_EXT() {
    const std::string testName = "EXT";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x4816;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_ILLEGAL() {
    const std::string testName = "ILLEGAL";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x4816;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_JMP() {
    const std::string testName = "JMP";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x4816;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_JSR() {
    const std::string testName = "JSR";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x4816;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_LEA() {
    const std::string testName = "LEA";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x4816;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_LINK() {
    const std::string testName = "LINK";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x4816;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_LSL_LSR_TO_REGISTER() {
    const std::string testName = "Test_LSL_LSR_TO_REGISTER";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x4816;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_LSL_LSR_TO_MEMORY() {
    const std::string testName = "Test_LSL_LSR_TO_MEMORY";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x4816;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_MOVE() {
    const std::string testName = "MOVE";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x4816;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_MOVEA() {
    const std::string testName = "MOVEA";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x4816;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_MOVEM() {
    const std::string testName = "MOVEM";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x4816;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_MOVEP() {
    const std::string testName = "MOVEP";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x4816;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_MOVEQ() {
    const std::string testName = "MOVEQ";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x4816;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_MULS() {
    const std::string testName = "MULS";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x4816;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_NEG() {
    const std::string testName = "NEG";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x4816;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_NEGX() {
    const std::string testName = "NEGX";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x4816;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_NOP() {
    const std::string testName = "NOP";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x4816;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_NOT() {
    const std::string testName = "NOT";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x4816;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_OR() {
    const std::string testName = "OR";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x4816;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_ORI() {
    const std::string testName = "ORI";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x1;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_ORI_TO_CCR() {
    const std::string testName = "ORI_TO_CCR";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x3D;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_ORI_TO_SR() {
    const std::string testName = "ORI_TO_SR";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x7D;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_PEA() {
    const std::string testName = "PEA";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x4816;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_RESET() {
    const std::string testName = "RESET";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x4E71;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_ROL_ROR_TO_REGISTER() {
    const std::string testName = "Test_ROL_ROR_TO_REGISTER";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x4816;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_ROL_ROR_TO_MEMORY() {
    const std::string testName = "Test_ROL_ROR_TO_MEMORY";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x4816;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_ROXL_ROXR_TO_MEMORY() {
    const std::string testName = "Test_ROXL_ROXR_TO_MEMORY";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x4816;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_ROXL_ROXR_TO_REGISTER() {
    const std::string testName = "Test_ROXL_ROXR_TO_REGISTER";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x4816;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_RTE() {
    const std::string testName = "RTE";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x4816;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_RTR() {
    const std::string testName = "RTR";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x4816;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_RTS() {
    const std::string testName = "RTS";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x4816;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_SBCD() {
    const std::string testName = "SBCD";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
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
bool Test_Scc() {
    const std::string testName = "Scc";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x4816;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_STOP() {
    const std::string testName = "STOP";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x4816;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_SUB() {
    const std::string testName = "SUB";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x4816;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_SUBA() {
    const std::string testName = "SUBA";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x4816;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_SUBI() {
    const std::string testName = "SUBI";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x4816;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_SUBQ() {
    const std::string testName = "SUBQ";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x4816;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_SUBX() {
    const std::string testName = "SUBX";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x4816;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_SWAP() {
    const std::string testName = "SWAP";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x4816;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_TAS() {
    const std::string testName = "TAS";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x4816;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_TRAP() {
    const std::string testName = "TRAP";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x4816;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_TRAPV() {
    const std::string testName = "TRAPV";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x4816;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_TST() {
    const std::string testName = "TST";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x4816;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

    std::cout << "End Test_" << testName << "()" << std::endl;
    return testResult;
}
bool Test_UNLK() {
    const std::string testName = "UNLK";

    std::cout << "Start Test_" << testName << "()" << std::endl;
    bool testResult = true;

    CPU_STATE_DEBUG state;
    M68k::SetCpuState(state);

    word opcode = 0x4816;
    std::cout << "\t\texecute test [" << testName << "] with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

    M68k::ExecuteOpcode(opcode);
    state = M68k::GetCpuState();

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

    CPU_STATE_DEBUG state;

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
    std::map < std::string, bool > TestResults;

    M68k::SetUnitTestsMode();

    Genesis::AllocM68kMemory();
    Genesis::M68KWriteMemoryLONG(0xE00000, 0xCAFE);

    TestResults.insert(std::pair < std::string, bool > ("Test_ABCD", Test_ABCD()));
    TestResults.insert(std::pair < std::string, bool > ("Test_ADD", Test_ADD()));

    TestResults.insert(std::pair < std::string, bool > ("Test_ADDA", Test_ADDA()));
    TestResults.insert(std::pair < std::string, bool > ("Test_ADDI", Test_ADDI()));
    TestResults.insert(std::pair < std::string, bool > ("Test_ADDQ", Test_ADDQ()));
    TestResults.insert(std::pair < std::string, bool > ("Test_ADDX", Test_ADDX()));
    TestResults.insert(std::pair < std::string, bool > ("Test_AND", Test_AND()));
    TestResults.insert(std::pair < std::string, bool > ("Test_ANDI", Test_ANDI()));
    TestResults.insert(std::pair < std::string, bool > ("Test_ASL_ASR_TO_MEMORY", Test_ASL_ASR_TO_MEMORY()));
    TestResults.insert(std::pair < std::string, bool > ("Test_ASL_ASR_TO_REGISTER", Test_ASL_ASR_TO_REGISTER()));
    TestResults.insert(std::pair < std::string, bool > ("Test_Bcc", Test_Bcc()));
    TestResults.insert(std::pair < std::string, bool > ("Test_BCHG", Test_BCHG()));
    TestResults.insert(std::pair < std::string, bool > ("Test_BCLR", Test_BCLR()));
    TestResults.insert(std::pair < std::string, bool > ("Test_BRA", Test_BRA()));
    TestResults.insert(std::pair < std::string, bool > ("Test_BSET", Test_BSET()));
    TestResults.insert(std::pair < std::string, bool > ("Test_BSR", Test_BSR()));
    TestResults.insert(std::pair < std::string, bool > ("Test_BTST", Test_BTST()));
    TestResults.insert(std::pair < std::string, bool > ("Test_CHK", Test_CHK()));
    TestResults.insert(std::pair < std::string, bool > ("Test_CLR", Test_CLR()));
    TestResults.insert(std::pair < std::string, bool > ("Test_CMP", Test_CMP()));
    TestResults.insert(std::pair < std::string, bool > ("Test_CMPA", Test_CMPA()));
    TestResults.insert(std::pair < std::string, bool > ("Test_CMPI", Test_CMPI()));
    TestResults.insert(std::pair < std::string, bool > ("Test_CMPM", Test_CMPM()));
    TestResults.insert(std::pair < std::string, bool > ("Test_DBcc", Test_DBcc()));
    TestResults.insert(std::pair < std::string, bool > ("Test_DIVS", Test_DIVS()));
    TestResults.insert(std::pair < std::string, bool > ("Test_EOR", Test_EOR()));
    TestResults.insert(std::pair < std::string, bool > ("Test_EORI", Test_EORI()));
    TestResults.insert(std::pair < std::string, bool > ("Test_EXG", Test_EXG()));
    TestResults.insert(std::pair < std::string, bool > ("Test_EXT", Test_EXT()));
    TestResults.insert(std::pair < std::string, bool > ("Test_ILLEGAL", Test_ILLEGAL()));
    TestResults.insert(std::pair < std::string, bool > ("Test_JMP", Test_JMP()));
    TestResults.insert(std::pair < std::string, bool > ("Test_JSR", Test_JSR()));
    TestResults.insert(std::pair < std::string, bool > ("Test_LEA", Test_LEA()));
    TestResults.insert(std::pair < std::string, bool > ("Test_LINK", Test_LINK()));
    TestResults.insert(std::pair < std::string, bool > ("Test_LSL_LSR_TO_MEMORY", Test_LSL_LSR_TO_MEMORY()));
    TestResults.insert(std::pair < std::string, bool > ("Test_LSL_LSR_TO_REGISTER", Test_LSL_LSR_TO_REGISTER()));
    TestResults.insert(std::pair < std::string, bool > ("Test_MOVE", Test_MOVE()));
    TestResults.insert(std::pair < std::string, bool > ("Test_MOVEA", Test_MOVEA()));
    TestResults.insert(std::pair < std::string, bool > ("Test_MOVEM", Test_MOVEM()));
    TestResults.insert(std::pair < std::string, bool > ("Test_MOVEP", Test_MOVEP()));
    TestResults.insert(std::pair < std::string, bool > ("Test_MOVEQ", Test_MOVEQ()));
    TestResults.insert(std::pair < std::string, bool > ("Test_MULS", Test_MULS()));
    TestResults.insert(std::pair < std::string, bool > ("Test_NEG", Test_NEG()));
    TestResults.insert(std::pair < std::string, bool > ("Test_NEGX", Test_NEGX()));
    TestResults.insert(std::pair < std::string, bool > ("Test_NOP", Test_NOP()));
    TestResults.insert(std::pair < std::string, bool > ("Test_NOT", Test_NOT()));
    TestResults.insert(std::pair < std::string, bool > ("Test_OR", Test_OR()));
    TestResults.insert(std::pair < std::string, bool > ("Test_ORI", Test_ORI()));
    TestResults.insert(std::pair < std::string, bool > ("Test_ORI_TO_CCR", Test_ORI_TO_CCR()));
    TestResults.insert(std::pair < std::string, bool > ("Test_ORI_TO_SR", Test_ORI_TO_SR()));
    TestResults.insert(std::pair < std::string, bool > ("Test_PEA", Test_PEA()));
    TestResults.insert(std::pair < std::string, bool > ("Test_RESET", Test_RESET()));
    TestResults.insert(std::pair < std::string, bool > ("Test_ROL_ROR_TO_REGISTER", Test_ROL_ROR_TO_REGISTER()));
    TestResults.insert(std::pair < std::string, bool > ("Test_ROL_ROR_TO_MEMORY", Test_ROL_ROR_TO_MEMORY()));
    TestResults.insert(std::pair < std::string, bool > ("Test_ROXL_ROXR_TO_MEMORY", Test_ROXL_ROXR_TO_MEMORY()));
    TestResults.insert(std::pair < std::string, bool > ("Test_ROXL_ROXR_TO_REGISTER", Test_ROXL_ROXR_TO_REGISTER()));
    TestResults.insert(std::pair < std::string, bool > ("Test_RTE", Test_RTE()));
    TestResults.insert(std::pair < std::string, bool > ("Test_RTR", Test_RTR()));
    TestResults.insert(std::pair < std::string, bool > ("Test_RTS", Test_RTS()));
    TestResults.insert(std::pair < std::string, bool > ("Test_Scc", Test_Scc()));
    TestResults.insert(std::pair < std::string, bool > ("Test_STOP", Test_STOP()));
    TestResults.insert(std::pair < std::string, bool > ("Test_SUB", Test_SUB()));
    TestResults.insert(std::pair < std::string, bool > ("Test_SUBA", Test_SUBA()));
    TestResults.insert(std::pair < std::string, bool > ("Test_SUBI", Test_SUBI()));
    TestResults.insert(std::pair < std::string, bool > ("Test_SUBQ", Test_SUBQ()));
    TestResults.insert(std::pair < std::string, bool > ("Test_SUBX", Test_SUBX()));
    TestResults.insert(std::pair < std::string, bool > ("Test_SWAP", Test_SWAP()));
    TestResults.insert(std::pair < std::string, bool > ("Test_TAS", Test_TAS()));
    TestResults.insert(std::pair < std::string, bool > ("Test_TRAP", Test_TRAP()));
    TestResults.insert(std::pair < std::string, bool > ("Test_TRAPV", Test_TRAPV()));
    TestResults.insert(std::pair < std::string, bool > ("Test_TST", Test_TST()));

    TestResults.insert(std::pair < std::string, bool > ("Test_SBCD", Test_SBCD()));
    TestResults.insert(std::pair < std::string, bool > ("Test_NBCD", Test_NBCD()));

    /**
     * END
     **/
    std::cout << "!!!!!!All Test Completed!!!!!!" << std::endl;
    int testErrorCount = 0;
    for (auto iterator = TestResults.begin(); iterator != TestResults.end(); iterator++) {
        if (!iterator -> second) {
            testErrorCount++;
            std::cout << iterator -> first << " did not pass the test" << std::endl;
        }
    }

    if (testErrorCount == 0) {
        std::cout << "There is no known error GOOD JOB !" << std::endl;
    } else {
        std::cout << "There is " << testErrorCount << " known errors !" << std::endl;
    }

    system("pause");
}