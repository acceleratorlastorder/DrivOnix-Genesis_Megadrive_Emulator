#ifndef DRIVONIX_AUDIO_HPP
#define DRIVONIX_AUDIO_HPP

#include <cstdint>
#include <vector>

// Advance before a timestamped register access. All time is in console master clocks.
namespace Audio
{
void Init(std::uint32_t masterClockHz, std::uint32_t outputSampleRate = 48000);
void Advance(std::uint64_t masterCycles);
std::uint8_t ReadFM(unsigned port);
void WriteFM(unsigned port, std::uint8_t value);
void WritePSG(std::uint8_t value);
std::vector<std::int16_t> DrainSamples();
bool FMInterrupt();
}

#endif
