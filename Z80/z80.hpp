#ifndef DRIVONIX_Z80_HPP
#define DRIVONIX_Z80_HPP

#include <cstdint>

namespace Z80
{
using Read68kCallback = std::uint8_t (*)(std::uint32_t address, void* user);
using Write68kCallback = void (*)(std::uint32_t address, std::uint8_t value, void* user);
// Callbacks serve both the banked 68k window and the fixed $7F00 VDP alias.
// VDP callbacks must perform 68000 byte-lane reads / duplicated-byte writes.
void Init(Read68kCallback read68k, Write68kCallback write68k, void* user);
// Advances BOTH Z80 and Audio, including while reset/bus-held. Do not separately
// call Audio::Advance for these same clocks. Sound writes occur on their Z80 tick.
void Advance(std::uint64_t masterCycles);
void SetBusRequest(bool asserted);
void SetReset(bool asserted);
bool BusGranted();
std::uint8_t Read68k(std::uint32_t offset);
void Write68k(std::uint32_t offset, std::uint8_t value);
void Interrupt(bool asserted);
std::uint64_t ExecutedCycles();
}

#endif
