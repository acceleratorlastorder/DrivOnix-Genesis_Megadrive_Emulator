#include "m68k.hpp"

M68k::M68k()
{
	this->programCounter = 0x200;
	this->CCR = 0;
	this->stop = false;
	this->supervisorStackPointer = 0;
	this->userStackPointer = 0;
	this->superVisorModeActivated = true;
	this->unitTests = false;
}

M68k& M68k::get()
{
	static M68k instance;
	return instance;
}

void M68k::SetUnitTestsMode()
{
	get().unitTests = true;
}

void M68k::Init()
{
	get().unitTests = false;
	get().CCR = 0x2700;
	get().stop = false;

	for(int i = 0; i < 8; ++i)
	{
		get().registerData[i] = 0x0;
		get().registerAddress[i] = 0x0;
	}

	get().registerAddress[0x7] = 0x0; //get().registerAddress[0x7] = readmem32(0x0);

	get().supervisorStackPointer = get().registerAddress[0x7];
	get().userStackPointer = get().registerAddress[0x7];
	get().superVisorModeActivated = true;
	get().programCounter = 0x0; //get().programCounter = readmem32(0x4);

	get().programCounterStart = get().programCounter; 

}

void M68k::SetCpuState(CPU_STATE_DEBUG cpuState)
{
	get().cpuStateDebug = cpuState;

	get().programCounter = get().cpuStateDebug.programCounter;
	get().CCR = get().cpuStateDebug.CCR;
	get().stop = get().cpuStateDebug.stop;

	for(int i = 0; i < 8; ++i)
	{
		get().registerData[i] = get().cpuStateDebug.registerData[i];
		get().registerAddress[i] = get().cpuStateDebug.registerAddress[i];
	}
}

CPU_STATE_DEBUG M68k::GetCpuState()
{
	return get().cpuStateDebug;
}

void M68k::ExecuteOpcode(word opcode)
{
	if((opcode & 0xC100) == 0xC100)
	{
		if(get().unitTests)
		{
			std::cout << "\tM68k :: Launch OpcodeABCD" << std::endl;
		}

		get().OpcodeABCD(opcode);
	}

	//copy state for unit test
	if(get().unitTests)
	{
		get().cpuStateDebug.programCounter = get().programCounter;
		get().cpuStateDebug.CCR = get().CCR;
		get().cpuStateDebug.stop = get().stop;

		for(int i = 0; i < 8; ++i)
		{
			get().cpuStateDebug.registerData[i] = get().registerData[i];
			get().cpuStateDebug.registerAddress[i] = get().registerAddress[i];
		}
	}
}

void M68k::CheckPrivilege()
{
	bool oldSuperVisorMode = get().superVisorModeActivated;

	get().superVisorModeActivated = TestBit(get().CCR, 13);

	if(oldSuperVisorMode != get().superVisorModeActivated)
	{
		if(oldSuperVisorMode)
		{
			get().supervisorStackPointer = get().registerAddress[0x7];
			get().registerAddress[0x7] = get().userStackPointer;
		}
		else
		{
			get().userStackPointer = get().registerAddress[0x7];
			get().registerAddress[0x7] = get().supervisorStackPointer;
		}
	}
}

int M68k::Update()
{
	//check Interrupt()

	get().CheckPrivilege();

	if(get().stop)
	{
		return 4;
	}

	get().opcodeClicks = 0;
	const word opcode = 0x0; //readmem16(programcounter);

	get().programCounterStart = get().programCounter;
	get().programCounter += 2;

	get().ExecuteOpcode(opcode);

	return get().opcodeClicks;
}

void M68k::SetDataRegister(int id, dword result, DATASIZE size)
{
	switch(size)
	{
		case BYTE:
		get().registerData[id] &= 0xFFFFFF00;
		get().registerData[id] |= (byte)result;
		break;

		case WORD:
		get().registerData[id] &= 0xFFFF0000;
		get().registerData[id] |= (word)result;
		break;

		case LONG:
		get().registerData[id] = result;
		break;
	}
}

word M68k::SignExtendWord(byte data)
{
	word result = data;
	if(TestBit(data, 7))
	{
		result |= 0xFF00;
	}
	return result;
}

dword M68k::SignExtendDWord(word data)
{
	dword result = data;
	if(TestBit(data, 15))
	{
		result |= 0xFFFF0000;
	}
	return res;
}

EA_DATA M68k::GetEAOperand(EA_TYPES mode, byte reg, DATASIZE size, bool readOnly, dword offset)
{
	EA_DATA result = EA_DATA();
	result.dataSize = size;

	switch(mode)
	{
		case EA_DATA_REG:
		result.operand = get().registerData[reg];
		result.eatype = EA_DATA_REG;
		break;

		case EA_ADDRESS_REG:
		result.operand = get().registerAddress[reg];
		result.eatype = EA_ADDRESS_REG;
		break;

		case EA_ADDRESS_REG_INDIRECT:
		{
			dword address = get().registerAddress[reg];
			address += offset;
			result.pointer = address;
			result.eatype = EA_ADDRESS_REG_INDIRECT;

			switch(size)
			{
				case BYTE:
				result.cycles = 4;
				result.operand = //readMem8(address);
				break;

				case WORD:
				result.cycles = 4;
				result.operand = //readmem16(address);
				break;

				case LONG:
				result.cycles = 8;
				result.operand = //readmem32(address);
				break;
			}
		}
		break;

		case EA_ADDRESS_REG_INDIRECT_POST_INC:
		{
			dword address = get().registerAddress[reg];
			result.pointer = address;
			result.eatype = EA_ADDRESS_REG_INDIRECT_POST_INC;

			switch(size)
			{
				case BYTE:
				{
					result.cycles = 4;
					result.operand = //readmem8(address)

					if(!readOnly)
					{
						get().registerAddress[reg] +=1;

						if(reg == 7) //le stackPointer est incrémenté par deux ici, pour garder son alignement (word)
						{
							get().registerAddress[reg] += 1;
						}
					}
				}
				break;

				case WORD:
				{
					result.cycles = 4;
					result.operand = //readmem16
					if(!readOnly)
					{
						get().registerAddress[reg] += 2;
					}
				}
				break;

				case LONG:
				{
					result.cycles = 8;
					result.operand = //readmem32()

					if(!readOnly)
					{
						get().registerAddress[reg] += 4;
					}
				}
				break;
			}
		}
		break;

		case EA_ADDRESS_REG_INDIRECT_PRE_DEC:
		{
			result.eatype = EA_ADDRESS_REG_INDIRECT_PRE_DEC;

			switch(size)
			{
				case BYTE:
				{
					result.cycles = 6;

					dword address = get().registerAddress[reg] - 1;

					if(reg == 7)//stackpointer alignement
					{
						address -= 1;
					}

					if(!readOnly)
					{
						get().registerAddress[reg] -= 1;

						if(reg == 7)//stackpointer alignement
						{
							get().registerAddress[reg] -= 1;
						}
					}

					result.pointer = address;
					result.operand = //readmem8(address);
				}
				break;

				case WORD:
				{
					result.cycles = 6;

					dword address = get().registerAddress[reg] - 2;

					if(!readOnly)
					{
						get().registerAddress[reg] -= 2;
					}

					result.pointer = address;
					result.operand = //readmem16(address)
				}
				break;

				case LONG:
				{
					result.cycles = 10;

					dword address = get().registerAddress[reg] - 4;

					if(!readOnly)
					{
						get().registerAddress[reg] -= 4;
					}

					result.pointer = address;
					result.operand = //readmem32(address);
				}
				break;
			}
		}
		break;

		case EA_ADDRESS_REG_INDIRECT_DISPLACEMENT:
		{
			result.eatype = EA_ADDRESS_REG_INDIRECT_DISPLACEMENT;
			result.PCadvance = 2;

			dword address = get().registerAddress[reg] + offset;

			signed_dword displacement = get().SignExtendDWord(/*readmem16(programcounter))*/);

			address += displacement;

			result.pointer = address;

			switch(size)
			{
				case BYTE:
				result.cycles = 8;
				result.operand = //readmem8(address)
				break;

				case WORD:
				result.cycles = 8;
				result.operand = //readmem16(address)
				break;

				case LONG:
				result.cycles = 12;
				result.operand = //readmem32(address)
				break;
			}
		}
		break;

		case EA_ADDRESS_REG_INDIRECT_INDEX:
		{
			result.eatype = EA_ADDRESS_REG_INDIRECT_INDEX;
			result.PCadvance = 2;

			word data = /*readmem16(programcounter)*/;

			bool inDataReg = !TestBit(data, 15);

			int regIndex = (data >> 12) & 0x7;

			bool isLong = TestBit(data, 11)

			byte displacementData = data & 0xFF;

			dword displacement = SignExtendDWord(SignExtendWord(displacementData));

			result.pointer = inDataReg ? get().registerData[regIndex] : get().registerAddress[regIndex];

			if(!isLong)
			{
				word pointer = result.pointer & 0xFFFF;
				result.pointer = SignExtendDWord(pointer);
			}

			int scale = (data >> 9) & 0x3;

			switch(scale)
			{
				case 1:
				result.pointer *= 2;
				break;

				case 2:
				result.pointer *= 4;
				break;

				case 3:
				result.pointer *= 8;
				break;
			}

			result.pointer += get().registerAddress[reg];
			result.pointer += displacement;
			result.pointer += offset;

			switch(size)
			{
				case BYTE:
				result.cycles = 10;
				result.operand = //readmem8(result.pointer);
				break;

				case WORD:
				result.cycles = 10;
				result.operand = //readmem16(result.pointer);
				break;

				case LONG:
				result.cycles = 14;
				result.operand = //readmem32(result.pointer);
				break;
			}
		}
		break;

		case EA_MODE_7:
		{
			result.eatype = EA_MODE_7;

			switch(reg)
			{
				case EA_MODE_7_ABS_ADDRESS_WORD:
				{
					result.mode7Type = EA_MODE_7_ABS_ADDRESS_WORD;
					result.pointer = SignExtendDWord(/*readmem16(programCounter)*/) + offset;
					result.PCadvance = 2;

					switch(size)
					{
						case BYTE:
						result.cycles = 8;
						result.operand = //readmem8(result.pointer);
						break;

						case WORD:
						result.cycles = 8;
						result.operand = //readmem16(result.pointer);
						break;

						case LONG:
						result.cycles = 12;
						result.operand = //readmem32(result.pointer);
						break;
					}
				}
				break;

				case EA_MODE_7_ABS_ADDRESS_LONG:
				{
					result.mode7Type = EA_MODE_7_ABS_ADDRESS_LONG;
					result.pointer = (/*readmem16(programcounter)*/ << 16) | /*readmem16(programcounter + 2)*/;
					result.pointer += offset;
					result.PCadvance = 4;

					switch(size)
					{
						case BYTE:
						result.cycles = 12;
						result.operand = //readmem8(result.pointer)
						break;

						case WORD:
						result.cycles = 12;
						result.operand = //readmem16(result.pointer)
						break;

						case LONG:
						result.cycles = 16;
						result.operand = //readmem32(result.pointer);
						break;
					}
				}
				break;

				case EA_MODE_7_PC_WITH_DISPLACEMENT:
				{
					result.mode7Type = EA_MODE_7_PC_WITH_DISPLACEMENT;
					result.pointer = get().programCounter + SignExtendDWord(/*readmem16(programcounter)*/) + offset;
					result.PCadvance = 2;

					switch(size)
					{
						case BYTE:
						result.cycles = 8;
						result.operand = //readmem8(result.pointer);
						break;

						case WORD:
						result.cycles = 8;
						result.operand = //readmem16(result.pointer);
						break;

						case LONG:
						result.cycles = 8;
						result.operand = //readmem32(result.pointer);
						break;
					}
				}
				break;

				case EA_MODE_7_PC_WITH_PREINDEX:
				{
					result.eatype = EA_MODE_7_PC_WITH_PREINDEX;
					result.PCadvance = 2;

					word data = /*readmem16(programcounter)*/;

					bool inDataReg = !TestBit(data, 15);

					int regIndex = (data >> 12) & 0x7;

					bool isLong = TestBit(data, 11)

					byte displacementData = data & 0xFF;

					dword displacement = SignExtendDWord(SignExtendWord(displacementData));

					result.pointer = inDataReg ? get().registerData[regIndex] : get().registerAddress[regIndex];

					if(!isLong)
					{
						word pointer = result.pointer & 0xFFFF;
						result.pointer = SignExtendDWord(pointer);
					}

					int scale = (data >> 9) & 0x3;

					switch(scale)
					{
						case 1:
						result.pointer *= 2;
						break;

						case 2:
						result.pointer *= 4;
						break;

						case 3:
						result.pointer *= 8;
						break;
					}

					result.pointer += get().programCounter;
					result.pointer += displacement;
					result.pointer += offset;

					switch(size)
					{
						case BYTE:
						result.cycles = 10;
						result.operand = //readmem8(result.pointer);
						break;

						case WORD:
						result.cycles = 10;
						result.operand = //readmem16(result.pointer);
						break;

						case LONG:
						result.cycles = 14;
						result.operand = //readmem32(result.pointer);
						break;
					}
				}
				break;

				case EA_MODE_7_IMMEDIATE_DATA:
				{
					result.mode7Type = EA_MODE_7_IMMEDIATE_DATA;
					result.pointer = get().programCounter;

					switch(size)
					{
						case BYTE:
						result.cycles = 4;
						result.operand = //readmem8(programCounter + 1)
						result.PCadvance = 2;
						break;

						case WORD:
						result.cycles = 4;
						result.operand = //readmem16(programCounter + 1)
						result.PCadvance = 2;
						break;

						case LONG:
						result.cycles = 8;
						result.operand = //readmem32(programCounter + 1)
						result.PCadvance = 4;
						break;
					}
				}
				break;
			}
		}
		break;
	}
	return result;
}
void M68k::SetAddressRegister(int id, dword result, DATASIZE size)
{
	switch(size)
	{
		case BYTE:
		std::cout << "M68k :: Error : Trying to write a byte to an address register" << std::endl;
		break;

		case WORD:
		get().registerAddress[id] = result;
		break;

		case LONG:
		get().registerAddress[id] = result;
	}
}

void M68k::OpcodeABCD(word opcode)
{
	byte rx = (opcode >> 9) & 0x7;
	byte rm = (opcode >> 3) & 0x7;
	byte ry = opcode & 0x7;


}