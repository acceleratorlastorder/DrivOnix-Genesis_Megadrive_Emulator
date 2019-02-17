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

	get().registerAddress[0x7] = Genesis::M68KReadMemoryLONG(0x0);

	get().supervisorStackPointer = get().registerAddress[0x7];
	get().userStackPointer = get().registerAddress[0x7];
	get().superVisorModeActivated = true;
	get().programCounter = Genesis::M68KReadMemoryLONG(0x4);

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
	const word opcode = Genesis::M68KReadMemoryWORD(get().programCounter);

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
	return result;
}

M68k::EA_DATA M68k::GetEAOperand(EA_TYPES mode, byte reg, DATASIZE size, bool readOnly, dword offset)
{
	EA_DATA result;
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
				result.operand = Genesis::M68KReadMemoryBYTE(address);
				break;

				case WORD:
				result.cycles = 4;
				result.operand = Genesis::M68KReadMemoryWORD(address);
				break;

				case LONG:
				result.cycles = 8;
				result.operand = Genesis::M68KReadMemoryLONG(address);
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
					result.operand = Genesis::M68KReadMemoryBYTE(address);

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
					result.operand = Genesis::M68KReadMemoryWORD(address);
					if(!readOnly)
					{
						get().registerAddress[reg] += 2;
					}
				}
				break;

				case LONG:
				{
					result.cycles = 8;
					result.operand = Genesis::M68KReadMemoryLONG(address);

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
					result.operand = Genesis::M68KReadMemoryBYTE(address);
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
					result.operand = Genesis::M68KReadMemoryWORD(address);
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
					result.operand = Genesis::M68KReadMemoryLONG(address);
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

			signed_dword displacement = get().SignExtendDWord(Genesis::M68KReadMemoryWORD(get().programCounter));

			address += displacement;

			result.pointer = address;

			switch(size)
			{
				case BYTE:
				result.cycles = 8;
				result.operand = Genesis::M68KReadMemoryBYTE(address);
				break;

				case WORD:
				result.cycles = 8;
				result.operand = Genesis::M68KReadMemoryWORD(address);
				break;

				case LONG:
				result.cycles = 12;
				result.operand = Genesis::M68KReadMemoryLONG(address);
				break;
			}
		}
		break;

		case EA_ADDRESS_REG_INDIRECT_INDEX:
		{
			result.eatype = EA_ADDRESS_REG_INDIRECT_INDEX;
			result.PCadvance = 2;

			word data = Genesis::M68KReadMemoryWORD(get().programCounter);

			bool inDataReg = !TestBit(data, 15);

			int regIndex = (data >> 12) & 0x7;

			bool isLong = TestBit(data, 11);

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
				result.operand = Genesis::M68KReadMemoryBYTE(result.pointer);
				break;

				case WORD:
				result.cycles = 10;
				result.operand = Genesis::M68KReadMemoryWORD(result.pointer);
				break;

				case LONG:
				result.cycles = 14;
				result.operand = Genesis::M68KReadMemoryLONG(result.pointer);
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
					result.pointer = SignExtendDWord(Genesis::M68KReadMemoryWORD(get().programCounter)) + offset;
					result.PCadvance = 2;

					switch(size)
					{
						case BYTE:
						result.cycles = 8;
						result.operand = Genesis::M68KReadMemoryBYTE(result.pointer);
						break;

						case WORD:
						result.cycles = 8;
						result.operand = Genesis::M68KReadMemoryWORD(result.pointer);
						break;

						case LONG:
						result.cycles = 12;
						result.operand = Genesis::M68KReadMemoryLONG(result.pointer);
						break;
					}
				}
				break;

				case EA_MODE_7_ABS_ADDRESS_LONG:
				{
					result.mode7Type = EA_MODE_7_ABS_ADDRESS_LONG;
					result.pointer = (Genesis::M68KReadMemoryWORD(get().programCounter) << 16) | Genesis::M68KReadMemoryWORD(get().programCounter + 2);
					result.pointer += offset;
					result.PCadvance = 4;

					switch(size)
					{
						case BYTE:
						result.cycles = 12;
						result.operand = Genesis::M68KReadMemoryBYTE(result.pointer);
						break;

						case WORD:
						result.cycles = 12;
						result.operand = Genesis::M68KReadMemoryWORD(result.pointer);
						break;

						case LONG:
						result.cycles = 16;
						result.operand = Genesis::M68KReadMemoryLONG(result.pointer);
						break;
					}
				}
				break;

				case EA_MODE_7_PC_WITH_DISPLACEMENT:
				{
					result.mode7Type = EA_MODE_7_PC_WITH_DISPLACEMENT;
					result.pointer = get().programCounter + SignExtendDWord(Genesis::M68KReadMemoryWORD(get().programCounter)) + offset;
					result.PCadvance = 2;

					switch(size)
					{
						case BYTE:
						result.cycles = 8;
						result.operand = Genesis::M68KReadMemoryBYTE(result.pointer);
						break;

						case WORD:
						result.cycles = 8;
						result.operand = Genesis::M68KReadMemoryWORD(result.pointer);
						break;

						case LONG:
						result.cycles = 8;
						result.operand = Genesis::M68KReadMemoryLONG(result.pointer);
						break;
					}
				}
				break;

				case EA_MODE_7_PC_WITH_PREINDEX:
				{
					result.mode7Type = EA_MODE_7_PC_WITH_PREINDEX;
					result.PCadvance = 2;

					word data = Genesis::M68KReadMemoryWORD(get().programCounter);

					bool inDataReg = !TestBit(data, 15);

					int regIndex = (data >> 12) & 0x7;

					bool isLong = TestBit(data, 11);

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
						result.operand = Genesis::M68KReadMemoryBYTE(result.pointer);
						break;

						case WORD:
						result.cycles = 10;
						result.operand = Genesis::M68KReadMemoryWORD(result.pointer);
						break;

						case LONG:
						result.cycles = 14;
						result.operand = Genesis::M68KReadMemoryLONG(result.pointer);
						break;
					}
				}
				break;

				case EA_MODE_7_IMMEDIATE:
				{
					result.mode7Type = EA_MODE_7_IMMEDIATE;
					result.pointer = get().programCounter;

					switch(size)
					{
						case BYTE:
						result.cycles = 4;
						result.operand = Genesis::M68KReadMemoryBYTE(get().programCounter + 1);
						result.PCadvance = 2;
						break;

						case WORD:
						result.cycles = 4;
						result.operand = Genesis::M68KReadMemoryWORD(get().programCounter);
						result.PCadvance = 2;
						break;

						case LONG:
						result.cycles = 8;
						result.operand = Genesis::M68KReadMemoryLONG(get().programCounter);
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

M68k::EA_DATA M68k::SetEAOperand(EA_TYPES mode, byte reg, dword data, M68k::DATASIZE size, dword offset)
{
	EA_DATA result;
	result.dataSize = size;

	switch(mode)
	{
		case EA_DATA_REG:
		SetDataRegister(reg, data, size);
		result.eatype = EA_DATA_REG;
		break;

		case EA_ADDRESS_REG:
		SetAddressRegister(reg, data, size);
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
				Genesis::M68KWriteMemoryBYTE(address, (byte)data);
				break;

				case WORD:
				result.cycles = 4;
				Genesis::M68KWriteMemoryWORD(address, (word)data);
				break;

				case LONG:
				result.cycles = 8;
				Genesis::M68KWriteMemoryLONG(address, data);
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
					Genesis::M68KWriteMemoryBYTE(address, (byte)data);

					get().registerAddress[reg] +=1;

					if(reg == 7) //le stackPointer est incrémenté par deux ici, pour garder son alignement (word)
					{
						get().registerAddress[reg] += 1;
					}
				}
				break;

				case WORD:
				{
					result.cycles = 4;
					Genesis::M68KWriteMemoryWORD(address, (word)data);

					get().registerAddress[reg] += 2;
				}
				break;

				case LONG:
				{
					result.cycles = 8;
					Genesis::M68KWriteMemoryLONG(address, data);

					get().registerAddress[reg] += 4;
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

					get().registerAddress[reg] -= 1;

					if(reg == 7)//stackpointer alignement
					{
						get().registerAddress[reg] -= 1;
					}

					result.pointer = address;
					Genesis::M68KWriteMemoryBYTE(address, (byte)data);
				}
				break;

				case WORD:
				{
					result.cycles = 6;

					dword address = get().registerAddress[reg] - 2;

					get().registerAddress[reg] -= 2;

					result.pointer = address;
					Genesis::M68KWriteMemoryWORD(address, (word)data);
				}
				break;

				case LONG:
				{
					result.cycles = 10;

					dword address = get().registerAddress[reg] - 4;

					get().registerAddress[reg] -= 4;

					result.pointer = address;
					Genesis::M68KWriteMemoryLONG(address, data);
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

			signed_dword displacement = get().SignExtendDWord(Genesis::M68KReadMemoryWORD(get().programCounter));

			address += displacement;

			result.pointer = address;

			switch(size)
			{
				case BYTE:
				result.cycles = 8;
				Genesis::M68KWriteMemoryBYTE(address, (byte)data);
				break;

				case WORD:
				result.cycles = 8;
				Genesis::M68KWriteMemoryWORD(address, (word)data);
				break;

				case LONG:
				result.cycles = 12;
				Genesis::M68KWriteMemoryLONG(address, data);
				break;
			}
		}
		break;

		case EA_ADDRESS_REG_INDIRECT_INDEX:
		{
			result.eatype = EA_ADDRESS_REG_INDIRECT_INDEX;
			result.PCadvance = 2;

			word Data = Genesis::M68KReadMemoryWORD(get().programCounter);

			bool inDataReg = !TestBit(Data, 15);

			int regIndex = (Data >> 12) & 0x7;

			bool isLong = TestBit(Data, 11);

			byte displacementData = Data & 0xFF;

			dword displacement = SignExtendDWord(SignExtendWord(displacementData));

			result.pointer = inDataReg ? get().registerData[regIndex] : get().registerAddress[regIndex];

			if(!isLong)
			{
				word pointer = result.pointer & 0xFFFF;
				result.pointer = SignExtendDWord(pointer);
			}

			int scale = (Data >> 9) & 0x3;

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
				Genesis::M68KWriteMemoryBYTE(result.pointer, (byte)data);
				break;

				case WORD:
				result.cycles = 10;
				Genesis::M68KWriteMemoryWORD(result.pointer, (word)data);
				break;

				case LONG:
				result.cycles = 14;
				Genesis::M68KWriteMemoryLONG(result.pointer, data);
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
					result.pointer = SignExtendDWord(Genesis::M68KReadMemoryWORD(get().programCounter)) + offset;
					result.PCadvance = 2;

					switch(size)
					{
						case BYTE:
						result.cycles = 8;
						Genesis::M68KWriteMemoryBYTE(result.pointer, (byte)data);
						break;

						case WORD:
						result.cycles = 8;
						Genesis::M68KWriteMemoryWORD(result.pointer, (word)data);
						break;

						case LONG:
						result.cycles = 12;
						Genesis::M68KWriteMemoryLONG(result.pointer, data);
						break;
					}
				}
				break;

				case EA_MODE_7_ABS_ADDRESS_LONG:
				{
					result.mode7Type = EA_MODE_7_ABS_ADDRESS_LONG;
					result.pointer = (Genesis::M68KReadMemoryWORD(get().programCounter) << 16) | Genesis::M68KReadMemoryWORD(get().programCounter + 2);
					result.pointer += offset;
					result.PCadvance = 4;

					switch(size)
					{
						case BYTE:
						result.cycles = 12;
						Genesis::M68KWriteMemoryBYTE(result.pointer, (byte)data);
						break;

						case WORD:
						result.cycles = 12;
						Genesis::M68KWriteMemoryWORD(result.pointer, (word)data);
						break;

						case LONG:
						result.cycles = 16;
						Genesis::M68KWriteMemoryLONG(result.pointer, data);
						break;
					}
				}
				break;

				case EA_MODE_7_PC_WITH_DISPLACEMENT:
				//Que pour le read
				break;

				case EA_MODE_7_PC_WITH_PREINDEX:
				{
					result.mode7Type = EA_MODE_7_PC_WITH_PREINDEX;
					result.PCadvance = 2;

					word Data = Genesis::M68KReadMemoryWORD(get().programCounter);

					bool inDataReg = !TestBit(Data, 15);

					int regIndex = (Data >> 12) & 0x7;

					bool isLong = TestBit(Data, 11);

					byte displacementData = Data & 0xFF;

					dword displacement = SignExtendDWord(SignExtendWord(displacementData));

					result.pointer = inDataReg ? get().registerData[regIndex] : get().registerAddress[regIndex];

					if(!isLong)
					{
						word pointer = result.pointer & 0xFFFF;
						result.pointer = SignExtendDWord(pointer);
					}

					int scale = (Data >> 9) & 0x3;

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
						Genesis::M68KWriteMemoryBYTE(result.pointer, (byte)data);
						break;

						case WORD:
						result.cycles = 10;
						Genesis::M68KWriteMemoryWORD(result.pointer, (word)data);
						break;

						case LONG:
						result.cycles = 14;
						Genesis::M68KWriteMemoryLONG(result.pointer, data);
						break;
					}
				}
				break;

				case EA_MODE_7_IMMEDIATE:
				//que pour le read
				break;
			}
		}
		break;
	}
	return result;
}

dword M68k::GetTypeMaxSize(DATASIZE size)
{
	switch(size)
	{
		case BYTE:
		return (dword)0xFF;
		break;

		case WORD:
		return (dword)0xFFFF;
		break;

		case LONG:
		return 0xFFFFFFFF;
		break;
	}

	return 0xFFFFFFFF;
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
	else if((opcode & 0xD000) == 0xD000)
	{
		if(get().unitTests)
		{
			std::cout << "\tM68k :: Launch OpcodeADD_ADDA" << std::endl;
		}

		get().OpcodeADD_ADDA(opcode);
	}
	else if((opcode & 0x0600) == 0x0600)
	{
		if(get().unitTests)
		{
			std::cout << "\tM68k :: Launch OpcodeADDI" << std::endl;
		}

		get().OpcodeADDI(opcode);
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

void M68k::OpcodeABCD(word opcode)
{
	byte rx = (opcode >> 9) & 0x7;
	byte rm = (opcode >> 3) & 0x1;
	byte ry = opcode & 0x7;

	byte src;
	byte dest;

	EA_TYPES type;

	if(rm)
	{
		type = EA_ADDRESS_REG_INDIRECT_PRE_DEC;
	}
	else
	{
		type = EA_DATA_REG;
	}

	
	dest = (byte)get().GetEAOperand(type, rx, BYTE, true, 0).operand;
	src = (byte)get().GetEAOperand(type, ry, BYTE, false, 0).operand;
	

	byte xflag = TestBit(get().CCR, X_FLAG);

	word result = dest + src + xflag;

	//BCD OVERFLOW CORRECTION
	if(((dest & 0xF) + (src & 0xF) + xflag) > 9)
	{
		result += 0x6;
	}

	if((result >> 4) > 9)
	{
		result += 0x60;
	}

	if(result > 0x99)
	{
		BitSet(get().CCR, C_FLAG);
		BitSet(get().CCR, X_FLAG);
	}
	else
	{
		BitReset(get().CCR, C_FLAG);
		BitReset(get().CCR, X_FLAG);
	}

	if((result & 0xFF) != 0)
	{
		BitReset(get().CCR, Z_FLAG);
	}

	
	EA_DATA data = get().SetEAOperand(type, rx, (byte)result, BYTE, 0);
	//cycles
}

void M68k::OpcodeADD_ADDA(word opcode)
{
	byte reg = (opcode >> 9) & 0x7;
	byte opmode = (opcode >> 6) & 0x7;
	byte eaMode = (opcode >> 3) & 0x7;
	byte eaReg = opcode & 0x7;

	if(!TestBit(opmode, 2))
	{
		//<EA> + DN -> DN
		DATASIZE size;
		switch(opmode)
		{
			case 0:
			size = BYTE;
			break;

			case 1:
			size = WORD;
			break;

			case 2:
			size = LONG;
			break;
		}

		EA_TYPES type = (EA_TYPES)eaMode;

		EA_DATA src = get().GetEAOperand(type, eaReg, size, false, 0);
		EA_DATA dest = get().GetEAOperand(EA_DATA_REG, reg, size, true, 0);

		//C_FLAG & X_FLAG
		uint64_t maxTypeSize = get().GetTypeMaxSize(size);
		uint64_t sonic = ((uint64_t)src.operand & maxTypeSize) + ((uint64_t)dest.operand & maxTypeSize);

		if(sonic > maxTypeSize)
		{
			BitSet(get().CCR, C_FLAG);
			BitSet(get().CCR, X_FLAG);
		}
		else
		{
			BitReset(get().CCR, C_FLAG);
			BitReset(get().CCR, X_FLAG);
		}

		//V_FLAG
		switch(size)
		{
			case BYTE:
			{
				signed_word eggman = (signed_byte)src.operand + (signed_byte)dest.operand;

				if(eggman > INT8_MAX)
				{
					BitSet(get().CCR, V_FLAG);
				}
				else
				{
					BitReset(get().CCR, V_FLAG);
				}
			}
			break;

			case WORD:
			{
				signed_dword eggman = (signed_word)src.operand + (signed_word)dest.operand;

				if(eggman > INT16_MAX)
				{
					BitSet(get().CCR, V_FLAG);
				}
				else
				{
					BitReset(get().CCR, V_FLAG);
				}
			}
			break;

			case LONG:
			{
				int64_t eggman = (signed_dword)src.operand + (signed_dword)dest.operand;

				if(eggman > INT32_MAX)
				{
					BitSet(get().CCR, V_FLAG);
				}
				else
				{
					BitReset(get().CCR, V_FLAG);
				}
			}
			break;
		}

		dword result = src.operand + dest.operand;

		//Z_FLAG
		if(result == 0)
		{
			BitSet(get().CCR, Z_FLAG);
		}
		else
		{
			BitReset(get().CCR, Z_FLAG);
		}

		//N_FLAG
		int bit;

		switch(size)
		{
			case BYTE:
			bit = 7;
			break;

			case WORD:
			bit = 15;
			break;

			case LONG:
			bit = 31;
			break;
		}

		if(TestBit(result, bit))
		{
			BitSet(get().CCR, N_FLAG);
		}
		else
		{
			BitReset(get().CCR, N_FLAG);
		}

		get().SetEAOperand(type, eaReg, result, size, 0);

		get().programCounter += src.PCadvance;

		//cycles

	}
	else
	{
		//DN + <EA> -> <EA>
		DATASIZE size;
		switch(opmode)
		{
			case 4:
			size = BYTE;
			break;

			case 5:
			size = WORD;
			break;

			case 6:
			size = LONG;
			break;
		}

		EA_TYPES type = (EA_TYPES)eaMode;

		EA_DATA src = get().GetEAOperand(type, eaReg, size, false, 0);
		EA_DATA dest = get().GetEAOperand(EA_DATA_REG, reg, size, true, 0);

		//C_FLAG & X_FLAG
		uint64_t maxTypeSize = get().GetTypeMaxSize(size);
		uint64_t sonic = ((uint64_t)dest.operand & maxTypeSize) + ((uint64_t)src.operand & maxTypeSize);

		if(sonic > maxTypeSize)
		{
			BitSet(get().CCR, C_FLAG);
			BitSet(get().CCR, X_FLAG);
		}
		else
		{
			BitReset(get().CCR, C_FLAG);
			BitReset(get().CCR, X_FLAG);
		}

		//V_FLAG
		switch(size)
		{
			case BYTE:
			{
				signed_word eggman = (signed_byte)dest.operand + (signed_byte)src.operand;

				if(eggman > INT8_MAX)
				{
					BitSet(get().CCR, V_FLAG);
				}
				else
				{
					BitReset(get().CCR, V_FLAG);
				}
			}
			break;

			case WORD:
			{
				signed_dword eggman = (signed_word)dest.operand + (signed_word)src.operand;

				if(eggman > INT16_MAX)
				{
					BitSet(get().CCR, V_FLAG);
				}
				else
				{
					BitReset(get().CCR, V_FLAG);
				}
			}
			break;

			case LONG:
			{
				int64_t eggman = (signed_dword)dest.operand + (signed_dword)src.operand;

				if(eggman > INT32_MAX)
				{
					BitSet(get().CCR, V_FLAG);
				}
				else
				{
					BitReset(get().CCR, V_FLAG);
				}
			}
			break;
		}

		dword result = dest.operand + src.operand;

		//ADDA Opcode
		if(size == WORD && type == EA_ADDRESS_REG)
		{
			result = SignExtendDWord((word)dest.operand) + SignExtendDWord((word)src.operand);
		}

		//Z_FLAG
		if(result == 0)
		{
			BitSet(get().CCR, Z_FLAG);
		}
		else
		{
			BitReset(get().CCR, Z_FLAG);
		}

		//N_FLAG
		int bit;

		switch(size)
		{
			case BYTE:
			bit = 7;
			break;

			case WORD:
			bit = 15;
			break;

			case LONG:
			bit = 31;
			break;
		}

		if(TestBit(result, bit))
		{
			BitSet(get().CCR, N_FLAG);
		}
		else
		{
			BitReset(get().CCR, N_FLAG);
		}

		get().SetEAOperand(type, reg, result, size, 0);

		get().programCounter += src.PCadvance;

		//cycles
	}
}

void M68k::OpcodeADDI(word opcode)
{
	DATASIZE size = (DATASIZE)((opcode >> 6) & 0x3);

	byte eaMode = (opcode >> 3) & 0x7;

	byte eaReg = opcode & 0x7;

	EA_TYPES type = (EA_TYPES)eaMode;

	EA_DATA src = get().GetEAOperand(EA_MODE_7, EA_MODE_7_IMMEDIATE, size, false, 0);
	EA_DATA dest = get().GetEAOperand(type, eaReg, size, true, 0);

	//C_FLAG & X_FLAG
	uint64_t maxTypeSize = get().GetTypeMaxSize(size);
	uint64_t sonic = ((uint64_t)dest.operand & maxTypeSize) + ((uint64_t)src.operand & maxTypeSize);

	if(sonic > maxTypeSize)
	{
		BitSet(get().CCR, C_FLAG);
		BitSet(get().CCR, X_FLAG);
	}
	else
	{
		BitReset(get().CCR, C_FLAG);
		BitReset(get().CCR, X_FLAG);
	}

	//V_FLAG
	switch(size)
	{
		case BYTE:
		{
			signed_word eggman = (signed_byte)dest.operand + (signed_byte)src.operand;

			if(eggman > INT8_MAX)
			{
				BitSet(get().CCR, V_FLAG);
			}
			else
			{
				BitReset(get().CCR, V_FLAG);
			}
		}
		break;

		case WORD:
		{
			signed_dword eggman = (signed_word)dest.operand + (signed_word)src.operand;

			if(eggman > INT16_MAX)
			{
				BitSet(get().CCR, V_FLAG);
			}
			else
			{
				BitReset(get().CCR, V_FLAG);
			}
		}
		break;

		case LONG:
		{
			int64_t eggman = (signed_dword)dest.operand + (signed_dword)src.operand;

			if(eggman > INT32_MAX)
			{
				BitSet(get().CCR, V_FLAG);
			}
			else
			{
				BitReset(get().CCR, V_FLAG);
			}
		}
		break;
	}

	dword result = dest.operand + src.operand;

	//Z_FLAG
	if(result == 0)
	{
		BitSet(get().CCR, Z_FLAG);
	}
	else
	{
		BitReset(get().CCR, Z_FLAG);
	}

	//N_FLAG
	int bit;

	switch(size)
	{
		case BYTE:
		bit = 7;
		break;

		case WORD:
		bit = 15;
		break;

		case LONG:
		bit = 31;
		break;
	}

	if(TestBit(result, bit))
	{
		BitSet(get().CCR, N_FLAG);
	}
	else
	{
		BitReset(get().CCR, N_FLAG);
	}

	get().SetEAOperand(type, eaReg, result, size, 0);

	get().programCounter += src.PCadvance;

	//cycles
}