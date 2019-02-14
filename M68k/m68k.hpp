#ifndef M68K_HPP
#define M68K_HPP

#include <iostream>
#include "../Bits/bitsUtils.hpp"

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
	M68k();
	static void SetUnitTestsMode();
	static void Init();
	static int Update();
	static void ExecuteOpcode(word opcode);

	//Start UnitTests
	static void SetCpuState(CPU_STATE_DEBUG cpuState);
	static CPU_STATE_DEBUG GetCpuState();
	//End UnitTests

private:

	static M68k& get(void);

	void CheckPrivilege();
	void SetDataRegister(int id, dword result, DATASIZE size);
	void SetAddressRegister(int id, dword result, DATASIZE size);
	word SignExtendWord(byte data);
	dword SignExtendDWord(word data);
	EA_DATA GetEAOperand(EA_TYPES mode, byte reg, DATASIZE size, bool readOnly, dword offset);
	EA_DATA SetEAOperand(EA_TYPES mode, byte reg, dword data, DATASIZE size, dword offset);	

	//Start opcodes
	void OpcodeABCD(word opcode);
	//End opcodes

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
	//---------------------------
	//Start CPU Reg, Flags, Var
	static const dword clockCyclesPerSecond = 7670000;

	static const int C_FLAG = 0;
	static const int V_FLAG = 1;
	static const int Z_FLAG = 2;
	static const int N_FLAG = 3;
	static const int X_FLAG = 4;

	dword programCounter;
	dword programCounterStart;
	dword opcodeClicks;
	word  CCR;
	bool  stop;

	dword registerData[8];
	dword registerAddress[8];
	dword supervisorStackPointer;
	dword userStackPointer;
	bool  superVisorModeActivated;
	//End CPU Reg, Flags, Var
	//--------------------------
	//Start CPU Unit Test
	bool unitTests;
	CPU_STATE_DEBUG cpuStateDebug;
	//End CPU Unit Test
	//--------------------------


};

#endif