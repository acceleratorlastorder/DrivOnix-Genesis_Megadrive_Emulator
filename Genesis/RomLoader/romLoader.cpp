#include "romLoader.hpp"

RomLoader::RomLoader()
{
	this->cartridgeSize = 0x0;
	this->haveSram = false;
	this->sramStart = 0x0;
	this->sramEnd = 0x0;
}

RomLoader& RomLoader::get()
{
	static RomLoader instance;
	return instance;
}

void RomLoader::LoadRomFile(std::string romName)
{
	std::string extension = romName.substr(romName.find_last_of("."));

	if(extension == ".bin")
	{
		std::cout << "Load BIN rom" << std::endl;
		get().LoadBINMD(romName);
	}
	else if(extension == ".md")
	{
		std::cout << "Load MD rom" << std::endl;
		get().LoadBINMD(romName);
	}
	else if(extension == ".smd")
	{
		std::cout << "Load SMD rom" << std::endl;
		get().LoadSMD(romName);
	}
	else
	{
		std::cout << "Error: Wrong Rom extension" << std::endl;
	}

	get().LoadHeader();
	get().Checksum();
}

void RomLoader::LoadBINMD(std::string romName)
{
	std::ifstream in(romName, std::ios::binary | std::ios::ate);

	get().cartridgeSize = (long)in.tellg();
	get().cartridgeMemory.reserve(get().cartridgeSize);

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
	InvertWordEndian(get().header.romStart);
	InvertWordEndian(get().header.romEnd);
	InvertWordEndian(get().header.romStart);
	InvertWordEndian(get().header.romEnd);
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
		std::cout << std::hex << "Calculated checksum value 0x" << checksum << " doesn't match with header checksum value. Header checksum isn't correct." << std::endl;

		get().cartridgeMemory[0x18E] = (checksum >> 8) & 0xFF;
		get().cartridgeMemory[0x18F] = (checksum & 0xFF);
		get().header.checksum = checksum;

		std::cout << "Checksum Fixed ! :)" << std::endl;
	}
	else
	{
		std::cout << std::hex << "Calculated checksum value 0x" << checksum << " match with header checksum value. Header checksum is correct." << std::endl;
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
	std::cout << std::hex << "Ram End : 0x" << get().header.ramStart << std::endl;
	std::cout << "Modem : " << modem << std::endl;
	std::cout << "Memo : " << memo << std::endl;
	std::cout << "Country : " << country << std::endl;
}
