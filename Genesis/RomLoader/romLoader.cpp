#include "romLoader.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace
{
std::vector<byte> ReadRom(const std::string& path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
        throw std::runtime_error("Cannot open ROM: " + path);
    const std::streamoff size = input.tellg();
    if (size < 0x200 || size > 0x800000 + SMD_HEADER_SIZE)
        throw std::runtime_error("ROM must contain a complete header and fit in the 8 MiB cartridge image limit: " + path);
    std::vector<byte> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), size))
        throw std::runtime_error("Cannot read ROM: " + path);
    return bytes;
}
}

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
    get().cartridgeMemory.clear();
    get().header = {};
    get().version = 0;
    get().computedChecksum = 0;
}

void RomLoader::PageMemory(int number, uint64_t pageSize, uint64_t start, std::vector<byte>& dest)
{
    if (number < 0 || pageSize == 0 || static_cast<uint64_t>(number) > get().cartridgeMemory.size() / pageSize)
        throw std::runtime_error("Invalid cartridge page");
    const uint64_t offset = pageSize * static_cast<uint64_t>(number);
    const uint64_t copySize = std::min<uint64_t>(pageSize, get().cartridgeMemory.size() - offset);
    if (start > dest.size() || copySize > dest.size() - start)
        throw std::runtime_error("Cartridge page exceeds destination memory");
    if (copySize)
        std::memcpy(dest.data() + start, get().cartridgeMemory.data() + offset, copySize);
}

const std::vector<byte>& RomLoader::Bytes() { return get().cartridgeMemory; }

byte RomLoader::GetCountry()
{
	return get().header.country[0];
}

void RomLoader::LoadRomFile(std::string romName)
{
    Init();
    const auto dot = romName.find_last_of('.');
    if (dot == std::string::npos)
        throw std::runtime_error("Unsupported ROM extension: " + romName);
	std::string extension = romName.substr(dot);
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char character) { return static_cast<char>(std::tolower(character)); });

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
		throw std::runtime_error("Unsupported ROM extension: " + romName);
	}

	get().LoadHeader();
	get().Checksum();
}

void RomLoader::LoadBINMDGEN(std::string romName)
{
    get().cartridgeMemory = ReadRom(romName);
    if (get().cartridgeMemory.size() > 0x800000)
        throw std::runtime_error("ROM exceeds the 8 MiB cartridge image limit: " + romName);
    get().cartridgeSize = static_cast<long>(get().cartridgeMemory.size());
}

void RomLoader::LoadSMD(std::string romName)
{
    const std::vector<byte> romSMD = ReadRom(romName);
    const std::size_t size = romSMD.size() - SMD_HEADER_SIZE;
    if (size == 0 || size % SMD_BLOCK_SIZE != 0)
        throw std::runtime_error("SMD ROM must contain complete 16 KiB blocks after its 512-byte header: " + romName);
    get().cartridgeSize = static_cast<long>(size);
    get().cartridgeMemory.resize(size);

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
    static_assert(sizeof(HEADER) == 0x100, "ROM header layout must match the cartridge format");
    std::memcpy(&get().header, get().cartridgeMemory.data() + 0x100, sizeof(HEADER));

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
        if (i + 1 < get().cartridgeSize)
		    checksum += get().cartridgeMemory[i + 1];
	}

    get().computedChecksum=checksum;
    if(checksum!=get().header.checksum)
        std::cerr<<"DrivOnix: ROM header checksum differs from the image; original bytes preserved\n";
}

bool RomLoader::ChecksumMatches() { return get().computedChecksum==get().header.checksum; }

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
