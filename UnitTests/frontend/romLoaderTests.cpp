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
    std::vector<byte> bytes(size);
    for (std::size_t i = 0; i < size; ++i) bytes[i] = static_cast<byte>(i * 37);
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
        std::cout << "ROM buffer resizing, bounds, checksum, PAL/NTSC and SMD decoding passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
