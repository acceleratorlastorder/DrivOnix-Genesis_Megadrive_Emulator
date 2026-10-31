#include "../../Genesis/RomLoader/romLoader.hpp"

#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace
{
void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void Write(const std::string& path, const std::vector<byte>& bytes)
{
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    Require(file.good(), "Cannot write loader fixture");
}

std::vector<byte> Cartridge(std::size_t size, byte country)
{
    if(size<0x200) throw std::invalid_argument("Cartridge fixture needs a complete header");
    std::vector<byte> bytes(size);
    for (std::size_t i = 0; i < size; ++i) bytes[i] = static_cast<byte>(i * 37);
    std::fill(bytes.begin()+0x1f0,bytes.begin()+0x200,' ');
    bytes[0x1f0] = country;
    word checksum = 0;
    for (std::size_t i = 0x200; i < size; i += 2)
    {
        checksum += bytes[i] << 8;
        if (i + 1 < size) checksum += bytes[i + 1];
    }
    bytes[0x18e] = checksum >> 8;
    bytes[0x18f] = checksum & 0xff;
    return bytes;
}

void Check(const std::string& path, const std::vector<byte>& expected, byte country)
{
    RomLoader::LoadRomFile(path);
    Require(RomLoader::GetCountry() == country, "Incorrect cartridge country");
    Require((RomLoader::GetVersion() & 0x40) == (country == 'E' ? 0x40 : 0), "Incorrect PAL/NTSC bit");
    std::vector<byte> memory(0x400000, 0xcc);
    RomLoader::PageMemory(0, memory.size(), 0, memory);
    Require(std::equal(expected.begin(), expected.end(), memory.begin()), "Cartridge bytes changed or SMD deinterleave failed");
    Require(std::all_of(memory.begin() + expected.size(), memory.end(), [](byte value) { return value == 0xcc; }),
            "Loader wrote past the end of the cartridge");
    std::vector<byte> page(512, 0xcc);
    RomLoader::PageMemory(1, 256, 32, page);
    Require(std::equal(expected.begin() + 256, expected.begin() + 512, page.begin() + 32), "Incorrect numbered cartridge page");
    bool rejected = false;
    try { RomLoader::PageMemory(0, 0x400000, 1, page); }
    catch (const std::exception&) { rejected = true; }
    Require(rejected, "An out-of-bounds destination was accepted");
}
void Regions(const std::string& prefix)
{
    struct Case {std::string field;byte selected;};
    const Case cases[]={
        {"J",'J'},{"U",'U'},{"E",'E'},{"JE",'J'},{"EU",'U'},{"JUE",'U'},
        {" j e ",'J'},{"USA",'U'},{"Japan",'J'},{"europe",'E'},
        {"1",'J'},{"4",'U'},{"8",'E'},{"9",'J'},{"A",'E'},{"B",'J'},
        {"C",'U'},{"D",'U'},{"F",'U'},{"0x1",'J'},{"0X8",'E'},{"0xE",'U'},{"0008",'E'},
        {"",'U'},{"0",'U'},{"2",'U'},{"?",'U'},{"UNKNOWN",'U'},{"16",'U'},{"0x",'U'},
        {"JAPAN?",'U'},{std::string("J\1",2),'U'},{std::string(16,char(0xff)),'U'},
        {std::string(16,'J'),'J'},{std::string("J\0\0",3),'J'}
    };
    for(const auto& item:cases)
    {
        auto rom=Cartridge(0x400,'U');
        std::fill(rom.begin()+0x1f0,rom.begin()+0x200,' ');
        std::copy(item.field.begin(),item.field.end(),rom.begin()+0x1f0);
        const auto path=prefix+"region.bin";
        Write(path,rom);RomLoader::LoadRomFile(path);
        const byte raw=item.field.empty() ? ' ':byte(item.field[0]);
        Require(RomLoader::GetCountry()==raw,"Region selection changed the raw country field");
        Require(RomLoader::GetHardwareRegion()==item.selected,"Incorrect canonical hardware region");
        Require(RomLoader::Bytes()==rom,"Region selection modified cartridge bytes");
        const byte version=item.selected=='J' ? 0:item.selected=='E' ? 0xc0:0x80;
        Require(RomLoader::GetVersion()==version,"Domestic/overseas or PAL/NTSC header version bits");
        Require(Genesis::GetHardwareRegion()==item.selected,"ROM region did not select console hardware");
        Require((Genesis::M68KReadMemoryBYTE(0xa10001)&0xc0)==version,"ASIC hardware region differs from header selection");
    }
    // Preserve the selected cartridge hardware while a differently tagged BIOS loads.
    for(byte region : {'J','E'})
    {
        auto rom=Cartridge(0x400,region);Write(prefix+"region.bin",rom);
        RomLoader::LoadRomFile(prefix+"region.bin");
        const byte selected=RomLoader::GetHardwareRegion();
        const auto bios=Cartridge(0x400,'U');Write(prefix+"region.bios",bios);
        RomLoader::LoadRomFile(prefix+"region.bios");
        Genesis::SetHardwareRegion(selected);Genesis::Init();
        const byte version=region=='J' ? 0:0xc0;
        Require((Genesis::M68KReadMemoryBYTE(0xa10001)&0xc0)==version,"BIOS load lost the selected cartridge hardware");
        RomLoader::LoadRomFile(prefix+"region.bin");Genesis::Init();
        Require(Genesis::GetHardwareRegion()==region,"Cartridge handoff lost its selected hardware region");
    }
    RomLoader::Init();Require(RomLoader::GetHardwareRegion()=='U',"Empty loader must default to USA hardware");
}
}

int main(int argc, char* argv[])
{
    try
    {
        Require(argc == 2, "Expected a writable fixture directory");
        const std::string prefix = std::string(argv[1]) + "/";
        // Grow, shrink and reload the real cartridge buffer, including odd sizes.
        for (std::size_t size : {0x200, 0x10000, 0x401, 0x4000})
        {
            const auto bytes = Cartridge(size, size == 0x401 ? 'E' : 'U');
            const auto path = prefix + "cartridge.BIN";
            Write(path, bytes);
            Check(path, bytes, bytes[0x1f0]);
        }
        auto modified=Cartridge(0x400,'U');modified[0x250]^=0x40;
        Write(prefix+"modified.bin",modified);
        Check(prefix+"modified.bin",modified,'U');
        Require(!RomLoader::ChecksumMatches(),"Checksum mismatch must remain observable without ROM mutation");
        const auto bytes = Cartridge(0x8000, 'E');
        std::vector<byte> smd(SMD_HEADER_SIZE + bytes.size());
        for (std::size_t block = 0; block < bytes.size(); block += SMD_BLOCK_SIZE)
            for (std::size_t i = 0; i < SMD_BLOCK_SIZE / 2; ++i)
            {
                smd[SMD_HEADER_SIZE + block + i] = bytes[block + i * 2 + 1];
                smd[SMD_HEADER_SIZE + block + SMD_BLOCK_SIZE / 2 + i] = bytes[block + i * 2];
            }
        Write(prefix + "cartridge.SMD", smd);
        Check(prefix + "cartridge.SMD", bytes, 'E');
        Regions(prefix);
        std::cout << "ROM resizing/bounds/checksum/SMD and canonical domestic/overseas region selection passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
