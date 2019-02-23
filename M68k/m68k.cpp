#include "m68k.hpp"

M68k::M68k()
{

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
	get().servicingInt = false;

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

void M68k::RequestAutoVectorInt(M68k::INT_TYPE level)
{
	int address = Genesis::M68KReadMemoryLONG(((int)level * 4) + 0x60);
	get().RequestInt(level, address);
}

void M68k::RequestInt(M68k::INT_TYPE level, int address)
{
	get().interrupts.insert(std::make_pair(level, address));
}

void M68k::CheckInt()
{
	if(get().interrupts.empty())
	{
		return;
	}

	std::pair<INT_TYPE, int> type = (*get().interrupts.rbegin());

	int intLevel = (get().CCR >> 8) & 0x7;

	if(type.first > INT_AUTOVECTOR_6 || type.first > intLevel)
	{
		word oldCCR = get().CCR;

		get().interrupts.erase(--get().interrupts.rbegin().base());

		BitSet(get().CCR, 13);

		get().CheckPrivilege();

		get().servicingInt = true;

		get().stop = false;

		get().registerAddress[0x7] -= 4;
		Genesis::M68KWriteMemoryLONG(get().registerAddress[0x7], get().programCounter);

		get().registerAddress[0x7] -= 2;
		Genesis::M68KWriteMemoryWORD(get().registerAddress[0x7], oldCCR);

		if(type.first <= INT_AUTOVECTOR_7)
		{
			get().CCR &= 0xF8FF;
			get().CCR |= type.first << 8;
		}

		get().programCounter = type.second;
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
	get().CheckInt();

	get().CheckPrivilege();

	if(get().stop)
	{
		return 4;
	}

	get().opcodeClicks = 0;
	const word opcode = Genesis::M68KReadMemoryWORD(get().programCounter);

	get().programCounter += 2;

	get().ExecuteOpcode(opcode);

	get().opcodeClicks = 10; //temporaire
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

			dword displacement = get().SignExtendDWord(get().SignExtendWord(displacementData));

			result.pointer = inDataReg ? get().registerData[regIndex] : get().registerAddress[regIndex];

			if(!isLong)
			{
				word pointer = result.pointer & 0xFFFF;
				result.pointer = get().SignExtendDWord(pointer);
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
					result.pointer = get().SignExtendDWord(Genesis::M68KReadMemoryWORD(get().programCounter)) + offset;
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
					result.pointer = get().programCounter + get().SignExtendDWord(Genesis::M68KReadMemoryWORD(get().programCounter)) + offset;
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

					dword displacement = get().SignExtendDWord(get().SignExtendWord(displacementData));

					result.pointer = inDataReg ? get().registerData[regIndex] : get().registerAddress[regIndex];

					if(!isLong)
					{
						word pointer = result.pointer & 0xFFFF;
						result.pointer = get().SignExtendDWord(pointer);
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

			dword displacement = get().SignExtendDWord(get().SignExtendWord(displacementData));

			result.pointer = inDataReg ? get().registerData[regIndex] : get().registerAddress[regIndex];

			if(!isLong)
			{
				word pointer = result.pointer & 0xFFFF;
				result.pointer = get().SignExtendDWord(pointer);
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
					result.pointer = get().SignExtendDWord(Genesis::M68KReadMemoryWORD(get().programCounter)) + offset;
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

					dword displacement = get().SignExtendDWord(get().SignExtendWord(displacementData));

					result.pointer = inDataReg ? get().registerData[regIndex] : get().registerAddress[regIndex];

					if(!isLong)
					{
						word pointer = result.pointer & 0xFFFF;
						result.pointer = get().SignExtendDWord(pointer);
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

bool M68k::ConditionTable(byte condition)
{
	switch(condition)
	{
		case 0:
		return true;
		break;

		case 1:
		return false;
		break;

		case 2:
		return (!TestBit(get().CCR, C_FLAG) && !TestBit(get().CCR, Z_FLAG));
		break;

		case 3:
		return (TestBit(get().CCR, C_FLAG) || TestBit(get().CCR, Z_FLAG));
		break;

		case 4:
		return !TestBit(get().CCR, C_FLAG);
		break;

		case 5:
		return TestBit(get().CCR, C_FLAG);
		break;

		case 6:
		return !TestBit(get().CCR, Z_FLAG);
		break;

		case 7:
		return TestBit(get().CCR, Z_FLAG);
		break;

		case 8:
		return !TestBit(get().CCR, V_FLAG);
		break;

		case 9:
		return TestBit(get().CCR, V_FLAG);
		break;

		case 10:
		return !TestBit(get().CCR, N_FLAG);
		break;

		case 11:
		return TestBit(get().CCR, N_FLAG);
		break;

		case 12:
		return (TestBit(get().CCR, N_FLAG) == TestBit(get().CCR, V_FLAG));
		break;

		case 13:
		return (TestBit(get().CCR, N_FLAG) != TestBit(get().CCR, V_FLAG));
		break;

		case 14:
		return ((TestBit(get().CCR, N_FLAG) == TestBit(get().CCR, V_FLAG)) && !TestBit(get().CCR, Z_FLAG));
		break;

		case 15:
		return ((TestBit(get().CCR, N_FLAG) != TestBit(get().CCR, V_FLAG)) || TestBit(get().CCR, Z_FLAG));
		break;
	}

	return false;
}

void M68k::ExecuteOpcode(word opcode)
{
	//get().SetUnitTestsMode();
	//std::cout << "\t ProgramCounter 0x" << std::hex << get().programCounter - 2 << std::endl;

	if((opcode & 0xF1F0) == 0xC100)
	{
		if(((opcode >> 3) & 0x1F) > 1)
		{
			if(get().unitTests)
			{
				std::cout << "\tM68k :: Execute OpcodeEXG" << std::endl;
			}

			std::cout << "\tM68k:: Unimplemented EXG Opcode" << std::endl;
			while(1);
		}
		else
		{
			if(get().unitTests)
			{
				std::cout << "\tM68k :: Execute OpcodeABCD" << std::endl;
			}

			get().OpcodeABCD(opcode);
		}
	}
	else if((opcode & 0xF000) == 0xD000)
	{
		if(get().unitTests)
		{
			std::cout << "\tM68k :: Execute OpcodeADD_ADDA" << std::endl;
		}

		get().OpcodeADD_ADDA(opcode);
	}
	else if((opcode & 0xFF00) == 0x0600)
	{
		if(get().unitTests)
		{
			std::cout << "\tM68k :: Execute OpcodeADDI" << std::endl;
		}

		get().OpcodeADDI(opcode);
	}
	else if((opcode & 0xF100) == 0x5000)
	{
		if(get().unitTests)
		{
			std::cout << "\tM68k :: Execute OpcodeADDQ_ADDA" << std::endl;
		}

		get().OpcodeADDQ_ADDA(opcode);
	}
	else if((opcode & 0xF130) == 0xD100)
	{
		if(get().unitTests)
		{
			std::cout << "\tM68k :: Execute OpcodeADDX" << std::endl;
		}

		get().OpcodeADDX(opcode);
	}
	else if((opcode & 0xF000) == 0xC000)
	{
		if(get().unitTests)
		{
			std::cout << "\tM68k :: Execute OpcodeAND" << std::endl;
		}

		get().OpcodeAND(opcode);
	}
	else if((opcode & 0xFF00) == 0x0200)
	{
		if(opcode == 0x023C)
		{
			if(get().unitTests)
			{
				std::cout << "\tM68k :: Execute OpcodeANDI_To_CCR" << std::endl;
			}

			get().OpcodeANDI_To_CCR();
		}
		else
		{
			if(get().unitTests)
			{
				std::cout << "\tM68k :: Execute OpcodeANDI" << std::endl;
			}

			get().OpcodeANDI(opcode);
		}
	}
	else if((opcode & 0xF018) == 0xE000)
	{
		if(get().unitTests)
		{
			std::cout << "\tM68k :: Execute OpcodeASL_ASR_Register" << std::endl;
		}

		get().OpcodeASL_ASR_Register(opcode);
	}
	else if((opcode & 0xFEC0) == 0xE0C0)
	{
		if(get().unitTests)
		{
			std::cout << "\tM68k :: Execute OpcodeASL_ASR_Memory" << std::endl;
		}

		get().OpcodeASL_ASR_Memory(opcode);
	}
	else if((opcode & 0xF000) == 0x6000)
	{
		if(get().unitTests)
		{
			std::cout << "\tM68k :: Execute OpcodeBCC" << std::endl;
		}

		get().OpcodeBCC(opcode);
	}
	else if((opcode & 0xF1C0) == 0x0140)
	{
		if(get().unitTests)
		{
			std::cout << "\tM68k :: Execute OpcodeBCHGDynamic" << std::endl;
		}

		get().OpcodeBCHGDynamic(opcode);
	}
	else if((opcode & 0xFFC0) == 0x0840)
	{
		if(get().unitTests)
		{
			std::cout << "\tM68k :: Execute OpcodeBCHGStatic" << std::endl;
		}

		get().OpcodeBCHGStatic(opcode);
	}
	else if((opcode & 0xF1C0) == 0x0180)
	{
		if(get().unitTests)
		{
			std::cout << "\tM68k :: Execute OpcodeBCLRDynamic" << std::endl;
		}

		get().OpcodeBCLRDynamic(opcode);
	}
	else if((opcode & 0xFFC0) == 0x0880)
	{
		if(get().unitTests)
		{
			std::cout << "\tM68k :: Execute OpcodeBCLRStatic" << std::endl;
		}

		get().OpcodeBCLRStatic(opcode);
	}
	else if((opcode & 0xF1C0) == 0x01C0)
	{
		if(get().unitTests)
		{
			std::cout << "\tM68k :: Execute OpcodeBSETDynamic" << std::endl;
		}

		get().OpcodeBSETDynamic(opcode);
	}
	else if((opcode & 0xFFC0) == 0x08C0)
	{
		if(get().unitTests)
		{
			std::cout << "\tM68k :: Execute OpcodeBSETStatic" << std::endl;
		}

		get().OpcodeBSETStatic(opcode);
	}
	else if((opcode & 0xFF00) == 0x6100)
	{
		if(get().unitTests)
		{
			std::cout << "\tM68k :: Execute OpcodeBSR" << std::endl;
		}

		get().OpcodeBSR(opcode);
	}
	else if((opcode & 0xF1C0) == 0x0100)
	{
		if(get().unitTests)
		{
			std::cout << "\tM68k :: Execute OpcodeBTSTDynamic" << std::endl;
		}

		get().OpcodeBTSTDynamic(opcode);
	}
	else if((opcode & 0xFFC0) == 0x0800)
	{
		if(get().unitTests)
		{
			std::cout << "\tM68k :: Execute OpcodeBTSTStatic" << std::endl;
		}

		get().OpcodeBTSTStatic(opcode);
	}
	else if((opcode & 0xF1C0) == 0x4180)
	{
		if(get().unitTests)
		{
			std::cout << "\tM68k :: Execute OpcodeCHK" << std::endl;
		}

		get().OpcodeCHK(opcode);
	}
	else if((opcode & 0xFF00) == 0x4200)
	{
		if(get().unitTests)
		{
			std::cout << "\tM68k :: Execute OpcodeCLR" << std::endl;
		}

		get().OpcodeCLR(opcode);
	}
	else if((opcode & 0xF100) == 0xB000)
	{
		if(get().unitTests)
		{
			std::cout << "\tM68k :: Execute OpcodeCMP_CMPA" << std::endl;
		}

		get().OpcodeCMP_CMPA(opcode);
	}
	else if((opcode & 0xFF00) == 0x0C00)
	{
		if(get().unitTests)
		{
			std::cout << "\tM68k :: Execute OpcodeCMPI" << std::endl;
		}

		get().OpcodeCMPI(opcode);
	}
	else if((opcode & 0xF138) == 0xB108)
	{
		if(get().unitTests)
		{
			std::cout << "\tM68k :: Execute OpcodeCMPM" << std::endl;
		}

		get().OpcodeCMPM(opcode);
	}
	else if((opcode & 0xF0F8) == 0x50C8)
	{
		if(get().unitTests)
		{
			std::cout << "\tM68k :: Execute OpcodeDBcc" << std::endl;
		}

		get().OpcodeDBcc(opcode);
	}
	else if((opcode & 0xFF00) == 0x4A00)
	{
		if(get().unitTests)
		{
			std::cout << "\tM68k :: Execute OpcodeTST" << std::endl;
		}

		get().OpcodeTST(opcode);
	}
	else
	{
		std::cout << "\t Unimplemented 0x" << std::hex << opcode << std::endl;
		while(1);
	}

	//std::cout << "\t 0x" << std::hex << opcode << std::endl;

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
		dword vectorman = result;
		switch(size)
		{
			case BYTE:
			vectorman &= 0xFF;
			break;

			case WORD:
			vectorman &= 0xFFFF;
			break;

			case LONG:
			vectorman &= 0xFFFFFFFF;
			break;
		}

		//Z_FLAG
		if(vectorman == 0)
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

		if(TestBit(vectorman, bit))
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
			result = get().SignExtendDWord((word)dest.operand) + get().SignExtendDWord((word)src.operand);
		}

		dword vectorman = result;
		switch(size)
		{
			case BYTE:
			vectorman &= 0xFF;
			break;

			case WORD:
			vectorman &= 0xFFFF;
			break;

			case LONG:
			vectorman &= 0xFFFFFFFF;
			break;
		}

		//Z_FLAG
		if(vectorman == 0)
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

		if(TestBit(vectorman, bit))
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

	get().programCounter += src.PCadvance;

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

	dword vectorman = result;
	switch(size)
	{
		case BYTE:
		vectorman &= 0xFF;
		break;

		case WORD:
		vectorman &= 0xFFFF;
		break;

		case LONG:
		vectorman &= 0xFFFFFFFF;
		break;
	}

	//Z_FLAG
	if(vectorman == 0)
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

	if(TestBit(vectorman, bit))
	{
		BitSet(get().CCR, N_FLAG);
	}
	else
	{
		BitReset(get().CCR, N_FLAG);
	}

	get().SetEAOperand(type, eaReg, result, size, 0);

	get().programCounter += dest.PCadvance;

	//cycles
}

void M68k::OpcodeADDQ_ADDA(word opcode)
{
	dword data = (opcode >> 9) & 0x7;

	DATASIZE size = (DATASIZE)((opcode >> 6) & 0x3);

	byte eaMode = (opcode >> 3) & 0x7;

	byte eaReg = opcode & 0x7;

	EA_TYPES type = (EA_TYPES)eaMode;

	data = (data == 0) ? 8 : data;

	EA_DATA dest = get().GetEAOperand(type, eaReg, size, true, 0);

	//ADDA
	if(type == EA_ADDRESS_REG && size == WORD)
	{
		data = get().SignExtendDWord((word)data);
	}

	dword result = data + dest.operand;

	get().SetEAOperand(type, eaReg, result, size, 0);

	if(type == EA_ADDRESS_REG && size == WORD)
	{
		size = LONG;
	}

	if(type == EA_ADDRESS_REG)
	{
		//C_FLAG & X_FLAG
		uint64_t maxTypeSize = get().GetTypeMaxSize(size);
		uint64_t sonic = ((uint64_t)data & maxTypeSize) + ((uint64_t)dest.operand & maxTypeSize);

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
				signed_word eggman = (signed_byte)data + (signed_byte)dest.operand;

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
				signed_dword eggman = (signed_word)data + (signed_word)dest.operand;

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
				int64_t eggman = (signed_dword)data + (signed_dword)dest.operand;

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

		dword vectorman = result;
		switch(size)
		{
			case BYTE:
			vectorman &= 0xFF;
			break;

			case WORD:
			vectorman &= 0xFFFF;
			break;

			case LONG:
			vectorman &= 0xFFFFFFFF;
			break;
		}

		//Z_FLAG
		if(vectorman == 0)
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

		if(TestBit(vectorman, bit))
		{
			BitSet(get().CCR, N_FLAG);
		}
		else
		{
			BitReset(get().CCR, N_FLAG);
		}
	}

	get().programCounter = dest.PCadvance;

	//cycles
}

void M68k::OpcodeADDX(word opcode)
{
	byte rx = (opcode >> 9) & 0x7;
	DATASIZE size = (DATASIZE)((opcode >> 6) & 0x3);
	byte rm = (opcode >> 3) & 0x1;
	byte ry = opcode & 0x7;

	EA_TYPES type;

	if(rm)
	{
		type = EA_ADDRESS_REG_INDIRECT_PRE_DEC;
	}
	else
	{
		type = EA_DATA_REG;
	}

	
	EA_DATA dest = get().GetEAOperand(type, rx, size, true, 0);
	EA_DATA src = get().GetEAOperand(type, ry, size, false, 0);
	

	byte xflag = TestBit(get().CCR, X_FLAG);

	//C_FLAG & X_FLAG
	uint64_t maxTypeSize = get().GetTypeMaxSize(size);
	uint64_t sonic = ((uint64_t)src.operand & maxTypeSize) + ((uint64_t)dest.operand & maxTypeSize) + (uint64_t)xflag;

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
			signed_word eggman = (signed_byte)src.operand + (signed_byte)dest.operand + (signed_byte)xflag;

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
			signed_dword eggman = (signed_word)src.operand + (signed_word)dest.operand + (signed_word)xflag;

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
			int64_t eggman = (signed_dword)src.operand + (signed_dword)dest.operand + (signed_dword)xflag;

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

	dword result = src.operand + dest.operand + xflag;

	dword vectorman = result;
	switch(size)
	{
		case BYTE:
		vectorman &= 0xFF;
		break;

		case WORD:
		vectorman &= 0xFFFF;
		break;

		case LONG:
		vectorman &= 0xFFFFFFFF;
		break;
	}

	//Z_FLAG
	if(vectorman != 0)
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

	if(TestBit(vectorman, bit))
	{
		BitSet(get().CCR, N_FLAG);
	}
	else
	{
		BitReset(get().CCR, N_FLAG);
	}
	
	EA_DATA data = get().SetEAOperand(type, rx, result, size, 0);
	//cycles
}

void M68k::OpcodeAND(word opcode)
{
	byte reg = (opcode >> 9) & 0x7;
	byte opmode = (opcode >> 6) & 0x7;
	byte eaMode = (opcode >> 3) & 0x7;
	byte eaReg = opcode & 0x7;

	if(!TestBit(opmode, 2))
	{
		//<EA> & DN -> DN
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

		//C_FLAG
		BitReset(get().CCR, C_FLAG);

		//V_FLAG
		BitReset(get().CCR, V_FLAG);

		dword result = src.operand & dest.operand;

		dword vectorman = result;
		switch(size)
		{
			case BYTE:
			vectorman &= 0xFF;
			break;

			case WORD:
			vectorman &= 0xFFFF;
			break;

			case LONG:
			vectorman &= 0xFFFFFFFF;
			break;
		}

		//Z_FLAG
		if(vectorman == 0)
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

		if(TestBit(vectorman, bit))
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
		//DN & <EA> -> <EA>
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

		//C_FLAG
		BitReset(get().CCR, C_FLAG);

		//V_FLAG
		BitReset(get().CCR, V_FLAG);

		dword result = dest.operand & src.operand;

		dword vectorman = result;
		switch(size)
		{
			case BYTE:
			vectorman &= 0xFF;
			break;

			case WORD:
			vectorman &= 0xFFFF;
			break;

			case LONG:
			vectorman &= 0xFFFFFFFF;
			break;
		}

		//Z_FLAG
		if(vectorman == 0)
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

		if(TestBit(vectorman, bit))
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

void M68k::OpcodeANDI(word opcode)
{
	DATASIZE size = (DATASIZE)((opcode >> 6) & 0x3);

	byte eaMode = (opcode >> 3) & 0x7;

	byte eaReg = opcode & 0x7;

	EA_TYPES type = (EA_TYPES)eaMode;

	EA_DATA src = get().GetEAOperand(EA_MODE_7, EA_MODE_7_IMMEDIATE, size, false, 0);
	EA_DATA dest = get().GetEAOperand(type, eaReg, size, true, 0);

	//C_FLAG
	BitReset(get().CCR, C_FLAG);

	//V_FLAG
	BitReset(get().CCR, V_FLAG);

	dword result = src.operand & dest.operand;

	dword vectorman = result;
	switch(size)
	{
		case BYTE:
		vectorman &= 0xFF;
		break;

		case WORD:
		vectorman &= 0xFFFF;
		break;

		case LONG:
		vectorman &= 0xFFFFFFFF;
		break;
	}

	//Z_FLAG
	if(vectorman == 0)
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

	if(TestBit(vectorman, bit))
	{
		BitSet(get().CCR, N_FLAG);
	}
	else
	{
		BitReset(get().CCR, N_FLAG);
	}

	get().SetEAOperand(type, eaReg, result, size, 0);

	get().programCounter += dest.PCadvance;

	//cycles
}

void M68k::OpcodeANDI_To_CCR()
{
	word data = Genesis::M68KReadMemoryWORD(get().programCounter);
	get().programCounter +=2;

	byte loCCR = (byte)get().CCR;
	byte loData = (byte)data;

	loCCR &= loData;
	get().CCR &= 0xFF00;
	get().CCR |= loCCR;

	//cycles
}

void M68k::OpcodeASL_ASR_Register(word opcode)
{
	byte count_reg = (opcode >> 9) & 0x7;

	byte dr = (opcode >> 8) & 0x1;

	DATASIZE size = (DATASIZE)((opcode >> 6) & 0x3);

	byte ir = (opcode >> 5) & 0x1;

	byte reg = opcode & 0x7;

	int shiftCount;

	if(ir)
	{
		shiftCount = get().registerData[count_reg] % 64;
	}
	else
	{
		shiftCount = (count_reg == 0) ? 8 : count_reg;  
	}

	signed_dword toShift = (signed_dword)get().registerData[reg];

	int numBits = 0;

	switch(size)
	{
		case BYTE:
		numBits = 8;
		toShift &= 0xFF;
		break;

		case WORD:
		numBits = 16;
		toShift &= 0xFFFF;
		break;

		case LONG:
		numBits = 32;
		break;
	}

	signed_dword result = 0;

	if(dr) //ShiftLeft
	{
		bool bitSet = TestBit(toShift, numBits - shiftCount);

		if(shiftCount == 0)
		{
			BitReset(get().CCR, C_FLAG);
		}
		else
		{
			if(bitSet)
			{
				BitSet(get().CCR, C_FLAG);
				BitSet(get().CCR, X_FLAG);
			}
			else
			{
				BitReset(get().CCR, C_FLAG);
				BitReset(get().CCR, X_FLAG);
			}
		}

		switch(size)
		{
			case BYTE:
			result = ((byte)toShift) << ((byte)shiftCount);
			break;

			case WORD:
			result = ((word)toShift) << ((word)shiftCount);
			break;

			case LONG:
			result = ((dword)toShift) << ((dword)shiftCount);
			break;
		}
	}
	else //ShiftRight
	{
		bool bitSet = TestBit(toShift, shiftCount - 1);

		if(shiftCount == 0)
		{
			BitReset(get().CCR, C_FLAG);
		}
		else
		{
			if(bitSet)
			{
				BitSet(get().CCR, C_FLAG);
				BitSet(get().CCR, X_FLAG);
			}
			else
			{
				BitReset(get().CCR, C_FLAG);
				BitReset(get().CCR, X_FLAG);
			}
		}

		switch(size)
		{
			case BYTE:
			result = ((byte)toShift) >> ((byte)shiftCount);
			break;

			case WORD:
			result = ((word)toShift) >> ((word)shiftCount);
			break;

			case LONG:
			result = ((dword)toShift) >> ((dword)shiftCount);
			break;
		}
	}

	BitReset(get().CCR, V_FLAG);

	dword vectorman = result;
	switch(size)
	{
		case BYTE:
		vectorman &= 0xFF;
		break;

		case WORD:
		vectorman &= 0xFFFF;
		break;

		case LONG:
		vectorman &= 0xFFFFFFFF;
		break;
	}

	//Z_FLAG
	if((vectorman) == 0)
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

	if(TestBit(vectorman, bit))
	{
		BitSet(get().CCR, N_FLAG);
	}
	else
	{
		BitReset(get().CCR, N_FLAG);
	}

	get().SetDataRegister(reg, (dword)result, size);

	//cycles
}

void M68k::OpcodeASL_ASR_Memory(word opcode)
{
	byte dr = (opcode >> 8) & 0x1;

	byte eaMode = (opcode >> 3) & 0x7;

	byte eaReg = opcode & 0x7;

	EA_TYPES type = (EA_TYPES)eaMode;

	signed_dword toShift = (signed_dword)get().GetEAOperand(type, eaReg, WORD, true, 0).operand;

	signed_dword result = 0;

	if(dr) //ShiftLeft
	{
		bool bitSet = TestBit(toShift, 15);

		if(bitSet)
		{
			BitSet(get().CCR, C_FLAG);
			BitSet(get().CCR, X_FLAG);
		}
		else
		{
			BitReset(get().CCR, C_FLAG);
			BitReset(get().CCR, X_FLAG);
		}

		result = ((signed_word)toShift) << ((signed_word)1);
	}
	else //ShiftRight
	{
		bool bitSet = TestBit(toShift, 0);

		if(bitSet)
		{
			BitSet(get().CCR, C_FLAG);
			BitSet(get().CCR, X_FLAG);
		}
		else
		{
			BitReset(get().CCR, C_FLAG);
			BitReset(get().CCR, X_FLAG);
		}

		result = ((signed_word)toShift) >> ((signed_word)1);
	}

	BitReset(get().CCR, V_FLAG);

	word vectorman = (word)result;
	vectorman &= 0xFFFF;

	//Z_FLAG
	if((vectorman) == 0)
	{
		BitSet(get().CCR, Z_FLAG);
	}
	else
	{
		BitReset(get().CCR, Z_FLAG);
	}

	//N_FLAG
	if(TestBit(vectorman, 15))
	{
		BitSet(get().CCR, N_FLAG);
	}
	else
	{
		BitReset(get().CCR, N_FLAG);
	}

	EA_DATA set = get().SetEAOperand(type, eaReg, (word)result, WORD, 0);

	get().programCounter += set.PCadvance;

	//cycles
}

void M68k::OpcodeBCC(word opcode)
{
	byte condition = (opcode >> 8) & 0xF;
	byte displacement8 = opcode & 0xFF;

	dword pcAdvance = 0; 

	dword displacement = get().SignExtendDWord(get().SignExtendWord(displacement8));

	if(displacement8 == 0)
	{
		word displacement16 = Genesis::M68KReadMemoryWORD(get().programCounter);
		pcAdvance = 2;
		displacement = get().SignExtendDWord(displacement16);
	}
	else if(displacement8 == 0xFF)
	{
		displacement = Genesis::M68KReadMemoryLONG(get().programCounter);
		pcAdvance = 4;
	}

	if(get().ConditionTable(condition))
	{
		get().programCounter += displacement;
	}
	else
	{
		get().programCounter += pcAdvance;
	}

	//cycles
}

void M68k::OpcodeBCHGDynamic(word opcode)
{
	byte reg = (opcode >> 9) & 0x7;
	byte eaMode = (opcode >> 3) & 0x7;
	byte eaReg = opcode & 0x7;

	EA_TYPES type = (EA_TYPES)eaMode;

	int modulo = (type == EA_ADDRESS_REG) ? 32 : 8;

	DATASIZE size = (type == EA_ADDRESS_REG) ? LONG : BYTE;

	int bit = get().registerData[reg] % modulo;
	
	EA_DATA dest = get().GetEAOperand(type, eaReg, size, true, 0);

	if(!TestBit(dest.operand, bit))
	{
		BitSet(get().CCR, Z_FLAG);
		BitSet(dest.operand, bit);
	}
	else
	{
		BitReset(get().CCR, Z_FLAG);
		BitReset(dest.operand, bit);
	}

	EA_DATA set = get().SetEAOperand(type, eaReg, dest.operand, size, 0);

	get().programCounter += dest.PCadvance;

	//cycles
}

void M68k::OpcodeBCHGStatic(word opcode)
{
	byte eaMode = (opcode >> 3) & 0x7;
	byte eaReg = opcode & 0x7;

	EA_TYPES type = (EA_TYPES)eaMode;

	int modulo = (type == EA_ADDRESS_REG) ? 32 : 8;

	DATASIZE size = (type == EA_ADDRESS_REG) ? LONG : BYTE;

	int bit = Genesis::M68KReadMemoryBYTE(get().programCounter + 1) % modulo;
	get().programCounter += 2;
	EA_DATA dest = get().GetEAOperand(type, eaReg, size, true, 0);

	if(!TestBit(dest.operand, bit))
	{
		BitSet(get().CCR, Z_FLAG);
		BitSet(dest.operand, bit);
	}
	else
	{
		BitReset(get().CCR, Z_FLAG);
		BitReset(dest.operand, bit);
	}

	EA_DATA set = get().SetEAOperand(type, eaReg, dest.operand, size, 0);

	get().programCounter += dest.PCadvance;

	//cycles
}

void M68k::OpcodeBCLRDynamic(word opcode)
{
	byte reg = (opcode >> 9) & 0x7;
	byte eaMode = (opcode >> 3) & 0x7;
	byte eaReg = opcode & 0x7;

	EA_TYPES type = (EA_TYPES)eaMode;

	int modulo = (type == EA_ADDRESS_REG) ? 32 : 8;

	DATASIZE size = (type == EA_ADDRESS_REG) ? LONG : BYTE;

	int bit = get().registerData[reg] % modulo;
	
	EA_DATA dest = get().GetEAOperand(type, eaReg, size, true, 0);

	if(!TestBit(dest.operand, bit))
	{
		BitSet(get().CCR, Z_FLAG);
	}
	else
	{
		BitReset(get().CCR, Z_FLAG);
	}

	BitReset(dest.operand, bit);

	EA_DATA set = get().SetEAOperand(type, eaReg, dest.operand, size, 0);

	get().programCounter += dest.PCadvance;

	//cycles
}

void M68k::OpcodeBCLRStatic(word opcode)
{
	byte eaMode = (opcode >> 3) & 0x7;
	byte eaReg = opcode & 0x7;

	EA_TYPES type = (EA_TYPES)eaMode;

	int modulo = (type == EA_ADDRESS_REG) ? 32 : 8;

	DATASIZE size = (type == EA_ADDRESS_REG) ? LONG : BYTE;

	int bit = Genesis::M68KReadMemoryBYTE(get().programCounter + 1) % modulo;
	get().programCounter += 2;
	EA_DATA dest = get().GetEAOperand(type, eaReg, size, true, 0);

	if(!TestBit(dest.operand, bit))
	{
		BitSet(get().CCR, Z_FLAG);
	}
	else
	{
		BitReset(get().CCR, Z_FLAG);
	}

	BitReset(dest.operand, bit);

	EA_DATA set = get().SetEAOperand(type, eaReg, dest.operand, size, 0);

	get().programCounter += dest.PCadvance;

	//cycles
}

void M68k::OpcodeBSETDynamic(word opcode)
{
	byte reg = (opcode >> 9) & 0x7;
	byte eaMode = (opcode >> 3) & 0x7;
	byte eaReg = opcode & 0x7;

	EA_TYPES type = (EA_TYPES)eaMode;

	int modulo = (type == EA_ADDRESS_REG) ? 32 : 8;

	DATASIZE size = (type == EA_ADDRESS_REG) ? LONG : BYTE;

	int bit = get().registerData[reg] % modulo;
	
	EA_DATA dest = get().GetEAOperand(type, eaReg, size, true, 0);

	if(!TestBit(dest.operand, bit))
	{
		BitSet(get().CCR, Z_FLAG);
	}
	else
	{
		BitReset(get().CCR, Z_FLAG);
	}

	BitSet(dest.operand, bit);

	EA_DATA set = get().SetEAOperand(type, eaReg, dest.operand, size, 0);

	get().programCounter += dest.PCadvance;

	//cycles
}

void M68k::OpcodeBSETStatic(word opcode)
{
	byte eaMode = (opcode >> 3) & 0x7;
	byte eaReg = opcode & 0x7;

	EA_TYPES type = (EA_TYPES)eaMode;

	int modulo = (type == EA_ADDRESS_REG) ? 32 : 8;

	DATASIZE size = (type == EA_ADDRESS_REG) ? LONG : BYTE;

	int bit = Genesis::M68KReadMemoryBYTE(get().programCounter + 1) % modulo;
	get().programCounter += 2;
	EA_DATA dest = get().GetEAOperand(type, eaReg, size, true, 0);

	if(!TestBit(dest.operand, bit))
	{
		BitSet(get().CCR, Z_FLAG);
	}
	else
	{
		BitReset(get().CCR, Z_FLAG);
	}

	BitSet(dest.operand, bit);

	EA_DATA set = get().SetEAOperand(type, eaReg, dest.operand, size, 0);

	get().programCounter += dest.PCadvance;

	//cycles
}

void M68k::OpcodeBSR(word opcode)
{
	byte displacement8 = opcode & 0xFF;

	dword pcAdvance = 0; 

	dword displacement = get().SignExtendDWord(get().SignExtendWord(displacement8));

	if(displacement8 == 0)
	{
		word displacement16 = Genesis::M68KReadMemoryWORD(get().programCounter);
		pcAdvance = 2;
		displacement = get().SignExtendDWord(displacement16);
	}
	else if(displacement8 == 0xFF)
	{
		displacement = Genesis::M68KReadMemoryLONG(get().programCounter);
		pcAdvance = 4;
	}	

	get().registerAddress[7] -= 4;
	Genesis::M68KWriteMemoryLONG(get().registerAddress[7], get().programCounter + pcAdvance);
	get().programCounter += displacement;

	//cycles
}

void M68k::OpcodeBTSTDynamic(word opcode)
{
	byte reg = (opcode >> 9) & 0x7;
	byte eaMode = (opcode >> 3) & 0x7;
	byte eaReg = opcode & 0x7;

	EA_TYPES type = (EA_TYPES)eaMode;

	int modulo = (type == EA_ADDRESS_REG) ? 32 : 8;

	DATASIZE size = (type == EA_ADDRESS_REG) ? LONG : BYTE;

	int bit = get().registerData[reg] % modulo;
	
	EA_DATA dest = get().GetEAOperand(type, eaReg, size, false, 0);

	if(!TestBit(dest.operand, bit))
	{
		BitSet(get().CCR, Z_FLAG);
	}
	else
	{
		BitReset(get().CCR, Z_FLAG);
	}

	get().programCounter += dest.PCadvance;

	//cycles
}

void M68k::OpcodeBTSTStatic(word opcode)
{
	byte eaMode = (opcode >> 3) & 0x7;
	byte eaReg = opcode & 0x7;

	EA_TYPES type = (EA_TYPES)eaMode;

	int modulo = (type == EA_ADDRESS_REG) ? 32 : 8;

	DATASIZE size = (type == EA_ADDRESS_REG) ? LONG : BYTE;

	int bit = Genesis::M68KReadMemoryBYTE(get().programCounter + 1) % modulo;
	get().programCounter += 2;
	EA_DATA dest = get().GetEAOperand(type, eaReg, size, false, 0);

	if(!TestBit(dest.operand, bit))
	{
		BitSet(get().CCR, Z_FLAG);
	}
	else
	{
		BitReset(get().CCR, Z_FLAG);
	}

	get().programCounter += dest.PCadvance;

	//cycles
}

void M68k::OpcodeCHK(word opcode)
{
	byte reg = (opcode >> 9) & 0x7;
	DATASIZE size = WORD; //long est juste dispo pour les M68020-30-40
	byte eaMode = (opcode >> 3) & 0x7;
	byte eaReg = opcode & 0x7;

	EA_TYPES type = (EA_TYPES)eaMode;

	signed_word Dn = get().registerData[reg];

	EA_DATA bounds = get().GetEAOperand(type, eaReg, size, false, 0);

	get().programCounter += bounds.PCadvance;

	//cycles

	if(Dn < 0)
	{
		//OpcodeTRAP(6);
		BitSet(get().CCR, N_FLAG);
	}
	else if(Dn > ((signed_word)bounds.operand))
	{
		//OpcodeTRAP(6);
		BitReset(get().CCR, N_FLAG);
	}
}

void M68k::OpcodeCLR(word opcode)
{
	DATASIZE size = (DATASIZE)((opcode >> 6) & 0x3);
	byte eaMode = (opcode >> 3) & 0x7;
	byte eaReg = opcode & 0x7;

	EA_TYPES type = (EA_TYPES)eaMode;

	EA_DATA set = get().SetEAOperand(type, eaReg, 0, size, 0);

	BitReset(get().CCR, N_FLAG);
	BitSet(get().CCR, Z_FLAG);
	BitReset(get().CCR, V_FLAG);
	BitReset(get().CCR, C_FLAG);

	get().programCounter += set.PCadvance;

	//cycles
}

void M68k::OpcodeCMP_CMPA(word opcode)
{
	byte reg = (opcode >> 9) & 0x7;
	byte opmode = (opcode >> 6) & 0x7;
	byte eaMode = (opcode >> 3) & 0x7;
	byte eaReg = opcode & 0x7;

	DATASIZE size;

	if(opmode > 2) //CMPA
	{
		switch(opmode)
		{
			case 3:
			size = WORD;
			break;

			case 7:
			size = LONG;
			break;
		}
	}
	else
	{
		size = (DATASIZE)opmode;
	}


	EA_TYPES type = (EA_TYPES)eaMode;

	EA_DATA src = get().GetEAOperand(type, eaReg, size, false, 0);

	dword dest;

	if(opmode > 2) //CMPA
	{
		dest = get().registerAddress[reg];

		if(size == WORD)
		{
			src.operand = get().SignExtendDWord((word)src.operand);
			size = LONG;
		}
	}
	else
	{
		dest = get().registerData[reg];
	}

	//C_FLAG
	uint64_t maxTypeSize = get().GetTypeMaxSize(size);
	uint64_t sonic = (uint64_t)dest & maxTypeSize;
	uint64_t tails = (uint64_t)src.operand & maxTypeSize;
	if(sonic < tails)
	{
		BitSet(get().CCR, C_FLAG);
	}
	else
	{
		BitReset(get().CCR, C_FLAG);
	}

	//V_FLAG
	switch(size)
	{
		case BYTE:
		{
			signed_word eggman = (signed_byte)dest - (signed_byte)src.operand;

			if(eggman < INT8_MIN)
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
			signed_dword eggman = (signed_word)dest - (signed_word)src.operand;

			if(eggman < INT16_MIN)
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
			int64_t eggman = (signed_dword)dest - (signed_dword)src.operand;

			if(eggman < INT32_MIN)
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

	dword result = dest - src.operand;

	dword vectorman = result;
	switch(size)
	{
		case BYTE:
		vectorman &= 0xFF;
		break;

		case WORD:
		vectorman &= 0xFFFF;
		break;

		case LONG:
		vectorman &= 0xFFFFFFFF;
		break;
	}

	//Z_FLAG
	if((vectorman) == 0)
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

	if(TestBit(vectorman, bit))
	{
		BitSet(get().CCR, N_FLAG);
	}
	else
	{
		BitReset(get().CCR, N_FLAG);
	}

	get().programCounter += src.PCadvance;

	//cycles
}

void M68k::OpcodeCMPI(word opcode)
{
	DATASIZE size = (DATASIZE)((opcode >> 6) & 0x3);

	byte eaMode = (opcode >> 3) & 0x7;

	byte eaReg = opcode & 0x7;

	EA_TYPES type = (EA_TYPES)eaMode;

	EA_DATA src = get().GetEAOperand(EA_MODE_7, EA_MODE_7_IMMEDIATE, size, false, 0);
	EA_DATA dest = get().GetEAOperand(type, eaReg, size, false, 0);

	get().programCounter += src.PCadvance;

	//C_FLAG
	uint64_t maxTypeSize = get().GetTypeMaxSize(size);
	uint64_t sonic = (uint64_t)dest.operand & maxTypeSize;
	uint64_t tails = (uint64_t)src.operand & maxTypeSize;
	if(sonic < tails)
	{
		BitSet(get().CCR, C_FLAG);
	}
	else
	{
		BitReset(get().CCR, C_FLAG);
	}

	//V_FLAG
	switch(size)
	{
		case BYTE:
		{
			signed_word eggman = (signed_byte)dest.operand - (signed_byte)src.operand;

			if(eggman < INT8_MIN)
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
			signed_dword eggman = (signed_word)dest.operand - (signed_word)src.operand;

			if(eggman < INT16_MIN)
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
			int64_t eggman = (signed_dword)dest.operand - (signed_dword)src.operand;

			if(eggman < INT32_MIN)
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

	dword result = dest.operand - src.operand;

	dword vectorman = result;
	switch(size)
	{
		case BYTE:
		vectorman &= 0xFF;
		break;

		case WORD:
		vectorman &= 0xFFFF;
		break;

		case LONG:
		vectorman &= 0xFFFFFFFF;
		break;
	}

	//Z_FLAG
	if((vectorman) == 0)
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

	if(TestBit(vectorman, bit))
	{
		BitSet(get().CCR, N_FLAG);
	}
	else
	{
		BitReset(get().CCR, N_FLAG);
	}

	get().programCounter += dest.PCadvance;

	//cycles
}

void M68k::OpcodeCMPM(word opcode)
{
	byte ax = (opcode >> 9) & 0x7;
	DATASIZE size = (DATASIZE)((opcode >> 6) & 0x3);
	byte ay = opcode & 0x7;

	EA_DATA dest = get().GetEAOperand(EA_ADDRESS_REG_INDIRECT_POST_INC, ax, size, false, 0);
	EA_DATA src = get().GetEAOperand(EA_ADDRESS_REG_INDIRECT_POST_INC, ay, size, false, 0);

	//C_FLAG
	uint64_t maxTypeSize = get().GetTypeMaxSize(size);
	uint64_t sonic = (uint64_t)dest.operand & maxTypeSize;
	uint64_t tails = (uint64_t)src.operand & maxTypeSize;
	if(sonic < tails)
	{
		BitSet(get().CCR, C_FLAG);
	}
	else
	{
		BitReset(get().CCR, C_FLAG);
	}

	//V_FLAG
	switch(size)
	{
		case BYTE:
		{
			signed_word eggman = (signed_byte)dest.operand - (signed_byte)src.operand;

			if(eggman < INT8_MIN)
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
			signed_dword eggman = (signed_word)dest.operand - (signed_word)src.operand;

			if(eggman < INT16_MIN)
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
			int64_t eggman = (signed_dword)dest.operand - (signed_dword)src.operand;

			if(eggman < INT32_MIN)
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

	dword result = dest.operand - src.operand;

	dword vectorman = result;
	switch(size)
	{
		case BYTE:
		vectorman &= 0xFF;
		break;

		case WORD:
		vectorman &= 0xFFFF;
		break;

		case LONG:
		vectorman &= 0xFFFFFFFF;
		break;
	}

	//Z_FLAG
	if((vectorman) == 0)
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

	if(TestBit(vectorman, bit))
	{
		BitSet(get().CCR, N_FLAG);
	}
	else
	{
		BitReset(get().CCR, N_FLAG);
	}

	get().programCounter += dest.PCadvance;

	//cycles
}

void M68k::OpcodeDBcc(word opcode)
{
	byte condition = (opcode >> 8) & 0xF;
	byte reg = opcode & 0x7;

	if(!get().ConditionTable(condition))
	{
		bool negOne = (get().registerData[reg] == 0);

		word lo = (word)((word)(get().registerData[reg] & 0xFFFF) - 1);
		word hi = (word)((word)((get().registerData[reg] >> 16) & 0xFFFF));

		get().registerData[reg] = (hi << 16) | lo;

		if(negOne)
		{
			get().registerData[reg] &= 0xFFFF0000;
			get().registerData[reg] |= 0xFFFF;

			get().programCounter += 2; 
		}
		else
		{
			dword displacement = get().SignExtendDWord(Genesis::M68KReadMemoryWORD(get().programCounter));

			get().programCounter += displacement;
		}
	}
	else
	{
		get().programCounter += 2;
	}

	//cycles
}

void M68k::OpcodeTST(word opcode)
{
	DATASIZE size = (DATASIZE)((opcode >> 6) & 0x3);
	byte eaMode = (opcode >> 3) & 0x7;
	byte eaReg = opcode & 0x7;

	EA_TYPES type = (EA_TYPES)eaMode;

	EA_DATA dest = get().GetEAOperand(type, eaReg, size, false, 0);

	BitReset(get().CCR, C_FLAG);
	BitReset(get().CCR, V_FLAG);

	dword vectorman = dest.operand;
	switch(size)
	{
		case BYTE:
		vectorman &= 0xFF;
		break;

		case WORD:
		vectorman &= 0xFFFF;
		break;

		case LONG:
		vectorman &= 0xFFFFFFFF;
		break;
	}

	//Z_FLAG
	if((vectorman) == 0)
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

	if(TestBit(vectorman, bit))
	{
		BitSet(get().CCR, N_FLAG);
	}
	else
	{
		BitReset(get().CCR, N_FLAG);
	}

	get().programCounter += dest.PCadvance;

	//cycles
}