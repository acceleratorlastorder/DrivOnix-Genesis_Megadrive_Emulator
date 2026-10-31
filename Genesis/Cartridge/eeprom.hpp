#ifndef DRIVONIX_EEPROM_HPP
#define DRIVONIX_EEPROM_HPP
#include <vector>
#include <cstdint>
#include "../../Bits/bitsUtils.hpp"

namespace EEPROM
{
enum class Profile
{
    None, SegaX24C01, EaX24C01, AcclaimX24C02,
    Acclaim24C02, Acclaim24C04, Acclaim24C16, Acclaim24C65,
    Codemasters24C08, Codemasters24C16, Codemasters24C65
};
Profile Detect(const std::vector<byte>& rom);
void Init(Profile profile, const std::vector<byte>& persisted = {});
bool IsMappedRead(dword address);
bool IsMappedWrite(dword address);
byte Read8(dword address, byte underlyingBus = 0xff);
void Write8(dword address, byte value);
void Write16(dword address, word value);
void WriteLines(bool scl, bool sda);
bool ReadSDA();
void SetMasterClock(std::uint32_t masterClockHz);
void Advance(std::uint64_t masterCycles);
const std::vector<byte>& Bytes();
bool Dirty();
void ClearDirty();
}
#endif
