#include "romloader.hpp"
#include "../../platform/archive.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <memory>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace
{
byte SelectRegion(const byte* field)
{
    std::string text;
    for(unsigned i=0;i<16;++i)
    {
        const byte value=field[i];
        // Region fields are ASCII, commonly padded with spaces or NULs.
        // Reject malformed control/non-ASCII bytes instead of guessing from them.
        if(value && (value<0x20 || value>0x7e)) return 'U';
        text.push_back(value ? char(value>='a' && value<='z' ? value-'a'+'A':value):' ');
    }
    const auto first=text.find_first_not_of(' ');
    if(first==std::string::npos) return 'U';
    text=text.substr(first,text.find_last_not_of(' ')-first+1);
    unsigned mask=0;
    if(text=="USA") mask=4;
    else if(text=="JAPAN") mask=1;
    else if(text=="EUROPE") mask=8;
    else
    {
        bool letters=true;
        for(char value:text)
        {
            if(value=='J') mask|=1;
            else if(value=='U') mask|=4;
            else if(value=='E') mask|=8;
            else if(value!=' ') { letters=false;break; }
        }
        if(!letters)
        {
            mask=0;
            unsigned at=text.compare(0,2,"0X")==0 ? 2:0;
            if(at==text.size()) return 'U';
            for(;at<text.size();++at)
            {
                const char value=text[at];
                const unsigned digit=value>='0' && value<='9' ? unsigned(value-'0'):
                    value>='A' && value<='F' ? unsigned(value-'A'+10):16;
                if(digit>15 || mask*16+digit>15) return 'U'; // Only the low region nibble is defined.
                mask=mask*16+digit;
            }
        }
    }
    // USA NTSC, domestic Japan NTSC, then overseas Europe PAL.
    // Bit1 denotes a domestic PAL system, which this base-console selector omits.
    return mask&4 ? 'U':mask&1 ? 'J':mask&8 ? 'E':'U';
}

std::vector<byte> ReadRom(const std::string& path)
{
    if(path.find('\0')!=std::string::npos)throw std::runtime_error("Invalid NUL in ROM path");
#ifdef _WIN32
    const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path.data(),int(path.size()),nullptr,0);
    if(!count)throw std::runtime_error("Invalid UTF-8 ROM path");
    std::wstring wide(count,L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path.data(),int(path.size()),&wide[0],count);
    std::unique_ptr<std::FILE,int(*)(std::FILE*)> input(_wfopen(wide.c_str(),L"rb"),std::fclose);
#else
    std::unique_ptr<std::FILE,int(*)(std::FILE*)> input(std::fopen(path.c_str(),"rb"),std::fclose);
#endif
    if(!input)
        throw std::runtime_error("Cannot open ROM: " + path);
    if(std::fseek(input.get(),0,SEEK_END)!=0)throw std::runtime_error("Cannot size ROM: "+path);
    const long size=std::ftell(input.get());
    if(size<0x200||size>0x800000+SMD_HEADER_SIZE)
        throw std::runtime_error("ROM must contain a complete header and fit in the 8 MiB cartridge image limit: " + path);
    std::vector<byte> bytes(static_cast<std::size_t>(size));
    if(std::fseek(input.get(),0,SEEK_SET)!=0||std::fread(bytes.data(),1,bytes.size(),input.get())!=bytes.size())
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
    get().version = 0x80;
    get().hardwareRegion = 'U';
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

byte RomLoader::GetHardwareRegion() { return get().hardwareRegion; }

void RomLoader::LoadRomFile(std::string romName,const std::string& member)
{
    Init();
    std::vector<byte> bytes;
    std::string cartridgeName=romName;
    if(Archive::IsArchive(romName))
    {
        auto selected=member;
        if(selected.empty())
        {
            const auto entries=Archive::List(romName);
            if(entries.size()!=1)throw std::runtime_error("Archive contains multiple games; select an exact archive member");
            selected=entries.front().name;
        }
        bytes=Archive::Read(romName,selected);cartridgeName=selected;
    }
    else
    {
        if(!member.empty())throw std::runtime_error("Archive member can only be used with a ZIP or 7z game");
    }
    const auto dot = cartridgeName.find_last_of('.');
    if(dot==std::string::npos)
        throw std::runtime_error("Unsupported ROM extension: "+cartridgeName);
    std::string extension=cartridgeName.substr(dot);
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char character) { return static_cast<char>(std::tolower(character)); });

    if(extension!=".bin"&&extension!=".md"&&extension!=".gen"&&extension!=".smd"&&extension!=".bios")
        throw std::runtime_error("Unsupported ROM extension: "+cartridgeName);
    if(bytes.empty())bytes=ReadRom(romName);
    if(extension==".smd") get().LoadSMD(bytes,cartridgeName);
    else
    {
        if(bytes.size()>0x800000)throw std::runtime_error("ROM exceeds the 8 MiB cartridge image limit: "+cartridgeName);
        get().cartridgeMemory=std::move(bytes);
        get().cartridgeSize=static_cast<long>(get().cartridgeMemory.size());
    }

	get().LoadHeader();
	get().Checksum();
}

void RomLoader::LoadImage(std::vector<byte> bytes)
{
    if(bytes.size()<0x200||bytes.size()>0x800000)
        throw std::runtime_error("Decoded ROM must contain a complete header and fit in the 8 MiB cartridge image limit");
    Init();get().cartridgeMemory=std::move(bytes);
    get().cartridgeSize=static_cast<long>(get().cartridgeMemory.size());
    get().LoadHeader();get().Checksum();
}

void RomLoader::LoadSMD(const std::vector<byte>& romSMD,const std::string& romName)
{
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

    get().hardwareRegion=SelectRegion(get().header.country);
    get().version=byte((get().hardwareRegion=='J' ? 0:0x80) |
                      (get().hardwareRegion=='E' ? 0x40:0));
    Genesis::SetHardwareRegion(get().hardwareRegion);
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
