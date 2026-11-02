#ifndef DRIVONIX_CARTRIDGE_HPP
#define DRIVONIX_CARTRIDGE_HPP
#include <string>
#include <vector>
#include "../../bits/bitsutils.hpp"
#include "eeprom.hpp"
namespace Cartridge {
struct Prepared {
    std::vector<byte> image,ram;
    std::string savePath;
    EEPROM::Prepared eeprom;
    dword ramStart=0,ramEnd=0;
    bool singleLane=false,enabled=false;
};
Prepared Prepare(const std::vector<byte>& rom,const std::string& path);
void Commit(Prepared prepared) noexcept;
void Load(const std::vector<byte>& rom, const std::string& path);
byte Read(dword address);
void Write(dword address, byte value);
void WriteWord(dword address, word value);
void Save();
bool HasSaveRAM();
}
#endif
