#include "genesis.hpp"

Genesis::Genesis()
{

}

void Genesis::Init()
{
	get().M68kMemory.reserve(M68K_MEM_SIZE);

	get().M68kMemory[0xA10009] = 0x00;
	get().M68kMemory[0xA1000B] = 0x00;
	get().M68kMemory[0xA1000D] = 0x00;
	get().M68kMemory[0xA1000F] = 0xFF;
	get().M68kMemory[0xA10011] = 0x00;
	get().M68kMemory[0xA10013] = 0x00;
	get().M68kMemory[0xA10015] = 0xFF;
	get().M68kMemory[0xA10017] = 0x00;
	get().M68kMemory[0xA10019] = 0x00;
	get().M68kMemory[0xA1001B] = 0xFB;
	get().M68kMemory[0xA1001D] = 0x00;
	get().M68kMemory[0xA1001F] = 0x00;

}

Genesis& Genesis::get(void)
{
	static Genesis instance;
	return instance;
}

void Genesis::FileBrowser()
{
	OPENFILENAME ofn;
	memset(&get().romName, 0, sizeof(get().romName));
    memset(&ofn,      0, sizeof(ofn));
    ofn.lStructSize  = sizeof(ofn);
    ofn.hwndOwner    = nullptr;
    ofn.lpstrFilter  = "Genesis/Megadrive BIN rom\0*.bin\0Genesis/Megadrive MD rom\0*.md\0Genesis/Megadrive SMD rom\0*.smd\0";
    ofn.lpstrFile    = get().romName;
    ofn.nMaxFile     = 2048;
    ofn.lpstrTitle   = "Select a Rom, hell yeah !";
    ofn.Flags        = OFN_DONTADDTORECENT | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    GetOpenFileNameA(&ofn);
}

char* Genesis::GetRomName()
{
	return get().romName;
}

byte Genesis::M68KReadMemoryBYTE(dword address)
{
	address = address % M68K_MEM_SIZE;

	//ROM Cartridge/Expension port
	if(address < 0xA00000)
	{
		return (byte)get().M68kMemory[address];
	}
	//Z80 Area
	else if(address >= 0xA00000 && address <= 0xA0FFFF)
	{
		return 0;
	}
	//I/O Registers
	else if(address >= 0xA10000 && address <= 0xA10FFF)
	{
		BitSet(address, 0);

		if(address == 0xA10001)
		{
			return (byte)RomLoader::GetVersion();
		}
		else if(address == 0xA10003)
		{
			//Manette 1
			return 0;
		}
		else if(address == 0xA10005)
		{
			//Manette 2
			return 0;
		}
		else if(address == 0xA10007)
		{
			//je sais pas, peut être le light gun
			return 0;
		}
		else
		{
			return (byte)get().M68kMemory[address];
		}
	}
	//Z80 Control
	else if(address >= 0xA11000 && address <= 0xA11FFF)
	{
		//z80bus
		return 0;
	}
	//VDP Ports
	else if(address >= 0xC00000 && address <= 0xDFFFFF)
	{
		if(address < 0xC00004)
		{
			word vdpword = /*YM7101::readdataport16()*/ 0;

			if((address % 2) == 0)
			{
				return (byte)(vdpword >> 8);
			}
			else
			{
				return (byte)vdpword;
			}
		}
		else if(address < 0xC00008)
		{
			word vdpword = /*YM7101::readcontrolport16()*/ 0;

			if((address % 2) == 0)
			{
				return (byte)(vdpword >> 8);
			}
			else
			{
				return (byte)vdpword;
			}
		}
		else if(address < 0xC0000F)
		{
			if((address % 2) == 0)
			{
				return 0; //return YM7101::GetVCounter()
			}
			else
			{
				return 0; //return YM7101::GetHCounter()
			}
		}
		else if(address == 0xC00011)
		{
			//PSG
			return 0;
		}
		else
		{
			return (byte)get().M68kMemory[address];
		}
	}
	//RAM (Mirrored every $FFFF)
	else if(address >= 0xE00000 && address <= 0xFFFFFF)
	{
		address = address % 0x100000;
		address += 0xE00000;
		return (byte)get().M68kMemory[address];
	}

	return 0;
}

void Genesis::M68KWriteMemoryBYTE(dword address, byte data)
{
	address = address % M68K_MEM_SIZE;

	//ROM Cartridge/Expension port
	if(address < 0xA00000)
	{
		//on peut pas écrire dans la rom
	}
	//Z80 Area
	else if(address >= 0xA00000 && address <= 0xA0FFFF)
	{
		//osef du z80
	}
	//I/O Registers
	else if(address >= 0xA10000 && address <= 0xA10FFF)
	{
		BitSet(address, 0);

		if(address == 0xA10003)
		{
			//Manette 1
		}
		else if(address == 0xA10005)
		{
			//Manette 2
		}
		else if(address == 0xA10007)
		{
			//je sais pas
		}
		else
		{
			get().M68kMemory[address] = data;
		}
	}
	//Z80 Control
	else if(address >= 0xA11000 && address <= 0xA11FFF)
	{
		//osef du z80
	}
	//VDP Ports
	else if(address >= 0xC00000 && address <= 0xDFFFFF)
	{
		if(address < 0xC00004)
		{
			//YM7101::Writedataport8(data);
		}
		else if(address < 0xC00008)
		{
			//YM7101::Writecontrolport8(data);
		}
		else if(address < 0xC00010)
		{
			//readonly
		}
		else if(address == 0xC00011)
		{
			//PSG
		}
		else
		{
			get().M68kMemory[address] = data;
		}
	}
	//RAM (Mirrored every $FFFF)
	else if(address >= 0xE00000 && address <= 0xFFFFFF)
	{
		address = address % 0x100000;
		address += 0xE00000;
		get().M68kMemory[address] = data;
	}
}

word Genesis::M68KReadMemoryWORD(dword address)
{
	address = address % M68K_MEM_SIZE;

	//ROM Cartridge/Expension port
	if(address < 0xA00000)
	{
		return (word)((get().M68kMemory[address] << 8) | get().M68kMemory[address + 1]);
	}
	//Z80 Area
	else if(address >= 0xA00000 && address <= 0xA0FFFF)
	{
		return 0;
	}
	//I/O Registers
	else if(address >= 0xA10000 && address <= 0xA10FFF)
	{
		BitSet(address, 0);

		if(address == 0xA10001)
		{
			return RomLoader::GetVersion();
		}
		else if(address == 0xA10003)
		{
			//Manette 1
			return 0;
		}
		else if(address == 0xA10005)
		{
			//Manette 2
			return 0;
		}
		else if(address == 0xA10007)
		{
			//je sais pas, peut être le light gun
			return 0;
		}
		else
		{
			return (word)get().M68kMemory[address];
		}
	}
	//Z80 Control
	else if(address >= 0xA11000 && address <= 0xA11FFF)
	{
		//z80bus
		return 0;
	}
	//VDP Ports
	else if(address >= 0xC00000 && address <= 0xDFFFFF)
	{
		if(address < 0xC00004)
		{
			return /*YM7101::readdataport16()*/ 0;
		}
		else if(address < 0xC00008)
		{
			return /*YM7101::readcontrolport16()*/ 0;
		}
		else if(address < 0xC0000F)
		{
			return /*YM7101::GetHVCounter();*/ 0;
		}
		else if(address == 0xC00011 || address == 0xC00013 || address == 0xC00015 || address == 0xC00017)
		{
			//PSG
			return 0;
		}
		else
		{
			return (word)((get().M68kMemory[address] << 8) | get().M68kMemory[address + 1]);
		}
	}
	//RAM (Mirrored every $FFFF)
	else if(address >= 0xE00000 && address <= 0xFFFFFF)
	{
		address = address % 0x100000;
		address += 0xE00000;
		return (word)((get().M68kMemory[address] << 8) | get().M68kMemory[address + 1]);
	}

	return 0;
}

void Genesis::M68KWriteMemoryWORD(dword address, word data)
{
	address = address % M68K_MEM_SIZE;

	//ROM Cartridge/Expension port
	if(address < 0xA00000)
	{
		//on peut pas écrire dans la rom
	}
	//Z80 Area
	else if(address >= 0xA00000 && address <= 0xA0FFFF)
	{
		//osef du z80
	}
	//I/O Registers
	else if(address >= 0xA10000 && address <= 0xA10FFF)
	{
		BitSet(address, 0);

		if(address == 0xA10003)
		{
			//Manette 1
		}
		else if(address == 0xA10005)
		{
			//Manette 2
		}
		else if(address == 0xA10007)
		{
			//je sais pas
		}
		else
		{
			get().M68kMemory[address] = (byte)data;
		}
	}
	//Z80 Control
	else if(address >= 0xA11000 && address <= 0xA11FFF)
	{
		//osef du z80
	}
	//VDP Ports
	else if(address >= 0xC00000 && address <= 0xDFFFFF)
	{
		if(address < 0xC00004)
		{
			//YM7101::Writedataport16(data);
		}
		else if(address < 0xC00008)
		{
			//YM7101::Writecontrolport16(data);
		}
		else if(address < 0xC00010)
		{
			//readonly
		}
		else if(address == 0xC00011 || address == 0xC00013 || address == 0xC00015 || address == 0xC00017)
		{
			//PSG
		}
		else
		{
			get().M68kMemory[address] = data;
		}
	}
	//RAM (Mirrored every $FFFF)
	else if(address >= 0xE00000 && address <= 0xFFFFFF)
	{
		address = address % 0x100000;
		address += 0xE00000;
		get().M68kMemory[address] = (byte)(data >> 8);
		get().M68kMemory[address + 1] = (byte)(data & 0xFF);
	}
}

dword Genesis::M68KReadMemoryLONG(dword address)
{
	return (dword)((get().M68KReadMemoryWORD(address) << 16) | get().M68KReadMemoryWORD(address + 2));
}

void Genesis::M68KWriteMemoryLONG(dword address, dword data)
{
	get().M68KWriteMemoryWORD(address, (word)(data >> 16));
	get().M68KWriteMemoryWORD(address + 2, (word)(data & 0xFFFF));
}
