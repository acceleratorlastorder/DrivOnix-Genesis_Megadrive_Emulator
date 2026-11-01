#ifndef DRIVONIX_CARTRIDGE_HPP
#define DRIVONIX_CARTRIDGE_HPP
#include <string>
#include <vector>
#include "../../bits/bitsutils.hpp"
namespace Cartridge {
void Load(const std::vector<byte>& rom, const std::string& path);
byte Read(dword address);
void Write(dword address, byte value);
void WriteWord(dword address, word value);
void Save();
bool HasSaveRAM();
}
#endif
