#include "romLoader.hpp"

RomLoader::RomLoader()
{

}

RomLoader& RomLoader::get()
{
	static RomLoader instance;
	return instance;
}

void RomLoader::Init()
{
	get().cartridgeSize = 0;
	get().sramSize = 0;
	get().haveSram = false;
}

void RomLoader::PageMemory(int number, uint64_t pageSize, uint64_t start, std::vector<byte>& dest)
{
	uint64_t copySize = std::min<uint64_t>(pageSize, get().cartridgeSize);

	if((number * pageSize) > (uint64_t)get().cartridgeSize)
	{
		std::cout << "Error: PageMemory" << std::endl;
	}
	else
	{
		std::memcpy(&dest[0] + start, &get().cartridgeMemory[0] + (pageSize * number), copySize);
	}
}

byte RomLoader::GetCountry()
{
	return get().header.country[0];
}

void RomLoader::LoadRomFile(std::string romName)
{
	std::string extension = romName.substr(romName.find_last_of("."));

	if(extension == ".bin")
	{
		get().LoadBINMDGEN(romName);
	}
	else if(extension == ".md")
	{
		get().LoadBINMDGEN(romName);
	}
	else if(extension == ".gen")
	{
		get().LoadBINMDGEN(romName);
	}
	else if(extension == ".smd")
	{
		get().LoadSMD(romName);
	}
	else if(extension == ".bios")
	{
		get().LoadBINMDGEN(romName);
	}
	else
	{
		std::cout << "Error: Wrong Rom extension" << std::endl;
	}

	get().LoadHeader();
	get().Checksum();
}

void RomLoader::LoadBINMDGEN(std::string romName)
{
	std::ifstream in(romName, std::ios::binary | std::ios::ate);

	get().cartridgeSize = (long)in.tellg();
	get().cartridgeMemory.reserve(get().cartridgeSize);
	std::fill(get().cartridgeMemory.begin(), get().cartridgeMemory.end(), 0);

	in.close();

	FILE *romFile = NULL;
	romFile = fopen(romName.c_str(), "rb");
	fread(&get().cartridgeMemory[0], get().cartridgeSize, 1, romFile);
	fclose(romFile);
}

void RomLoader::LoadSMD(std::string romName)
{
	std::ifstream in(romName, std::ios::binary | std::ios::ate);

	long romSize = (long)in.tellg();
	get().cartridgeSize = (long)((long)in.tellg() - (long)SMD_HEADER_SIZE);
	get().cartridgeMemory.reserve(get().cartridgeSize);
	std::fill(get().cartridgeMemory.begin(), get().cartridgeMemory.end(), 0);

	std::vector<byte> romSMD;
	romSMD.reserve(romSize);

	in.close();

	FILE *romFile = NULL;
	romFile = fopen(romName.c_str(), "rb");
	fread(&romSMD[0], romSize, 1, romFile);
	fclose(romFile);

	int blocksNumber = (get().cartridgeSize) / SMD_BLOCK_SIZE;

	for(int i = 0; i < blocksNumber; ++i)
	{
		int even = 0;
		int odd = 1;

		byte blockSMD[SMD_BLOCK_SIZE];
		byte blockBIN[SMD_BLOCK_SIZE];

		std::memcpy(blockSMD, &romSMD[0] + SMD_HEADER_SIZE + (i * SMD_BLOCK_SIZE), SMD_BLOCK_SIZE);

		const int middlepoint = SMD_BLOCK_SIZE / 2;

		for(int j = 0; j < SMD_BLOCK_SIZE; ++j)
		{
			if(j < middlepoint)
			{
				blockBIN[odd] = blockSMD[j];
				odd += 2;
			}
			else
			{
				blockBIN[even] = blockSMD[j];
				even += 2;
			}
		}

		std::memcpy(&get().cartridgeMemory[0] + (i * SMD_BLOCK_SIZE), blockBIN, SMD_BLOCK_SIZE);
	}
}

void RomLoader::LoadHeader()
{
	get().header = *((HEADER*)&get().cartridgeMemory[0x100]);

	InvertWordEndian(get().header.checksum);
	InvertDWordEndian(get().header.romStart);
	InvertDWordEndian(get().header.romEnd);
	InvertDWordEndian(get().header.ramStart);
	InvertDWordEndian(get().header.ramEnd);

	get().version = 0x0;

	BitSet(version, 7); //overseas
	if(get().header.country[0] == 'J' || get().header.country[0] == 'U')
	{
		BitReset(version, 6); //NTSC
		Genesis::SetRegionToPAL(false);
	}
	else if(get().header.country[0] == 'E' || get().header.country[0] == 'F')
	{
		BitSet(version, 6); //PAL
		Genesis::SetRegionToPAL(true);
	}
	else
	{
		BitReset(version, 6); //NTSC
		Genesis::SetRegionToPAL(false);
	}
}

byte RomLoader::GetVersion()
{
	return get().version;
}

void RomLoader::Checksum()
{
	word checksum = 0;
	for(long i = 0x200; i < get().cartridgeSize; i += 2)
	{
		checksum += get().cartridgeMemory[i] * 256;
		checksum += cartridgeMemory[i + 1];
	}

	if(checksum != get().header.checksum)
	{
		get().cartridgeMemory[0x18E] = (checksum >> 8) & 0xFF;
		get().cartridgeMemory[0x18F] = (checksum & 0xFF);
		get().header.checksum = checksum;
	}
}

void RomLoader::PrintRomHeader()
{
	std::string consoleName(&get().header.consoleName[0x0], &get().header.consoleName[0x10]);
	std::string copyright(&get().header.copyright[0x0], &get().header.copyright[0x10]);
	std::string domesticName(&get().header.domesticName[0x0], &get().header.domesticName[0x30]);
	std::string overseasName(&get().header.overseasName[0x0], &get().header.overseasName[0x30]);
	std::string production(&get().header.production[0x0], &get().header.production[0xE]);
	std::string PCodeVNumber(&get().header.PCodeVNumber[0x0], &get().header.PCodeVNumber[0x10]);
	std::string modem(&get().header.modem[0x0], &get().header.modem[0xC]);
	std::string memo(&get().header.memo[0x0], &get().header.memo[0x28]);
	std::string country(&get().header.country[0x0], &get().header.country[0x10]);

	std::cout << "Rom header Content : " << std::endl << std::endl;

	std::cout << "Console Name : " << consoleName << std::endl;
	std::cout << "Copyright : " << copyright << std::endl;
	std::cout << "Domestic Name : " << domesticName << std::endl;
	std::cout << "Overseas Name : " << overseasName << std::endl;
	std::cout << "Production : " << production << std::endl;
	std::cout << std::hex << "Checksum : 0x" << get().header.checksum << std::endl;
	std::cout << "Production Code / Version Number : " << PCodeVNumber << std::endl;
	std::cout << std::hex << "Rom Start : 0x" << get().header.romStart << std::endl;
	std::cout << std::hex << "Rom End : 0x" << get().header.romEnd << std::endl;
	std::cout << std::hex << "Ram Start : 0x" << get().header.ramStart << std::endl;
	std::cout << std::hex << "Ram End : 0x" << get().header.ramEnd << std::endl;
	std::cout << "Modem : " << modem << std::endl;
	std::cout << "Memo : " << memo << std::endl;
	std::cout << "Country : " << country << std::endl;
}
