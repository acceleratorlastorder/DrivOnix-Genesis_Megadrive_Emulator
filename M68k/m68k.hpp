#ifndef M68K_HPP
#define M68K_HPP

#include <iostream>
#include <map>
#include <string>
#include "../Bits/bitsUtils.hpp"
#include "../Genesis/genesis.hpp"

#define C_FLAG 0
#define V_FLAG 1
#define Z_FLAG 2
#define N_FLAG 3
#define X_FLAG 4

#define M68K_CYCLES_PER_SECOND 7670000


typedef struct
{
	dword programCounter;
	word  CCR;
	bool  stop;

	dword registerData[8];
	dword registerAddress[8];

}CPU_STATE_DEBUG;

class M68k
{
public:

	enum INT_TYPE
	{
		INT_AUTOVECTOR_0,
		INT_AUTOVECTOR_1,
		INT_AUTOVECTOR_2,
		INT_AUTOVECTOR_3,
		INT_AUTOVECTOR_4,
		INT_AUTOVECTOR_5,
		INT_AUTOVECTOR_6,
		INT_AUTOVECTOR_7,
		INT_TRAP,
		INT_PRIV_VIO 
	};

	M68k();
	static void SetUnitTestsMode();
	static void Init();
	static int Update();
	static void ExecuteOpcode(word opcode);
	static void RequestAutoVectorInt(INT_TYPE level);

	//Start UnitTests
	static void SetCpuState(CPU_STATE_DEBUG cpuState);
	static CPU_STATE_DEBUG GetCpuState();
	//End UnitTests

	//Start EA
	enum DATASIZE
	{
		BYTE,
		WORD,
		LONG
	};

	enum EA_TYPES
	{
		EA_DATA_REG,
		EA_ADDRESS_REG,
		EA_ADDRESS_REG_INDIRECT,
		EA_ADDRESS_REG_INDIRECT_POST_INC,
		EA_ADDRESS_REG_INDIRECT_PRE_DEC,
		EA_ADDRESS_REG_INDIRECT_DISPLACEMENT,
		EA_ADDRESS_REG_INDIRECT_INDEX,
		EA_MODE_7
	};

	enum EA_MODE_7_TYPES
	{
		EA_MODE_7_INVALID			   = -1,
		EA_MODE_7_ABS_ADDRESS_WORD     =  0,
		EA_MODE_7_ABS_ADDRESS_LONG     =  1,
		EA_MODE_7_PC_WITH_DISPLACEMENT =  2,
		EA_MODE_7_PC_WITH_PREINDEX	   =  3,
		EA_MODE_7_IMMEDIATE			   =  4,
		EA_MODE_7_NUM				   =  5
	};

	static const int EA_NUMBER = EA_MODE_7 + EA_MODE_7_NUM;

	struct EA_DATA
	{
		int cycles;
		dword operand;
		dword pointer;
		int PCadvance;
		EA_TYPES eatype;
		EA_MODE_7_TYPES mode7Type;
		DATASIZE dataSize;

		EA_DATA(): cycles(0),
				   operand(0),
				   pointer(0),
				   PCadvance(0),
				   eatype(EA_DATA_REG),
				   mode7Type(EA_MODE_7_INVALID),
				   dataSize(WORD){}
	};
	//End EA

private:

	static M68k& get(void);

	void RequestInt(INT_TYPE level, int address);
	void CheckInt();
	void CheckPrivilege();
	void SetDataRegister(int id, dword result, DATASIZE size);
	void SetAddressRegister(int id, dword result, DATASIZE size);
	word SignExtendWord(byte data);
	dword SignExtendDWord(word data);
	EA_DATA GetEAOperand(EA_TYPES mode, byte reg, DATASIZE size, bool readOnly, dword offset);
	EA_DATA SetEAOperand(EA_TYPES mode, byte reg, dword data, DATASIZE size, dword offset);	

	//Start opcodes Utils
	dword GetTypeMaxSize(DATASIZE size);
	bool ConditionTable(byte condition);
	bool IsOpcode(word opcode, std::string mask);
	//End opcodes Utils

	//Start opcodes
	void OpcodeABCD(word opcode);
	void OpcodeADD_ADDA(word opcode);
	void OpcodeADDI(word opcode);
	void OpcodeADDQ_ADDA(word opcode);
	void OpcodeADDX(word opcode);
	void OpcodeAND(word opcode);
	void OpcodeANDI(word opcode);
	void OpcodeANDI_To_CCR();
	void OpcodeASL_ASR_Register(word opcode);
	void OpcodeASL_ASR_Memory(word opcode);
	void OpcodeBcc(word opcode);
	void OpcodeBCHGDynamic(word opcode);
	void OpcodeBCHGStatic(word opcode);
	void OpcodeBCLRDynamic(word opcode);
	void OpcodeBCLRStatic(word opcode);
	void OpcodeBSETDynamic(word opcode);
	void OpcodeBSETStatic(word opcode);
	void OpcodeBSR(word opcode);
	void OpcodeBTSTDynamic(word opcode);
	void OpcodeBTSTStatic(word opcode);
	void OpcodeCHK(word opcode);
	void OpcodeCLR(word opcode);
	void OpcodeCMP_CMPA(word opcode);
	void OpcodeCMPI(word opcode);
	void OpcodeCMPM(word opcode);
	void OpcodeDBcc(word opcode);

	void OpcodeLEA(word opcode);
	void OpcodeMOVE(word opcode);
	void OpcodeMOVE_To_CCR(word opcode);
	void OpcodeMOVE_To_SR(word opcode);
	void OpcodeMOVE_From_SR(word opcode);
	void OpcodeMOVE_USP(word opcode);
	void OpcodeMOVEA(word opcode);
	void OpcodeMOVEM(word opcode);
	void OpcodeMOVEQ(word opcode);
	void OpcodeTST(word opcode);
	//End opcodes
	//---------------------------
	//Start CPU Reg, Flags, Var

	dword programCounter;
	dword opcodeClicks;
	word  CCR;
	bool  stop;

	dword registerData[8];
	dword registerAddress[8];
	dword supervisorStackPointer;
	dword userStackPointer;
	bool  superVisorModeActivated;

	std::multimap<INT_TYPE, int> interrupts;
	bool servicingInt;
	//End CPU Reg, Flags, Var
	//--------------------------
	//Start CPU Unit Test
	bool unitTests;
	CPU_STATE_DEBUG cpuStateDebug;
	//End CPU Unit Test
	//--------------------------


};

#endif