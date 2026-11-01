#include "z80.hpp"
#include "../audio/audio.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
std::vector<std::uint8_t> memory(0x1000000);
std::uint8_t Read(std::uint32_t address, void*) { return memory[address & 0xffffff]; }
void Write(std::uint32_t address, std::uint8_t value, void*) { memory[address & 0xffffff] = value; }
void OwnBus()
{
    Z80::SetBusRequest(true);
    Z80::Advance(15 * 200);
    Require(Z80::BusGranted(), "Z80 bus request was not acknowledged");
}
void Program(const std::vector<std::uint8_t>& program)
{
    OwnBus();
    for (std::size_t i = 0; i < program.size(); ++i) Z80::Write68k(i, program[i]);
    Z80::SetReset(false);
    Z80::SetBusRequest(false);
}
void Fm(unsigned address, std::uint8_t value)
{
    Audio::WriteFM(0,address);
    Audio::WriteFM(1,value);
}
void TestZ80()
{
    Audio::Init(53693175);
    Z80::Init(Read,Write,nullptr);
    Z80::Advance(53693175);
    Require(Z80::ExecutedCycles() == 0, "Z80 ran while held in reset");
    Require(Audio::DrainSamples().size() == 96000, "Audio stopped while Z80 was held in reset");
    std::vector<std::uint8_t> program = {
        0x31,0xf0,0x1f, // LD SP,1FF0
        0x3e,0x2a,0x32,0x00,0x01, // LD A,2A; LD (0100),A
        0xdd,0x21,0x20,0x01, // LD IX,0120
        0xdd,0x36,0x02,0x5a, // LD (IX+2),5A
        0xdd,0xcb,0x02,0x46, // BIT 0,(IX+2), must set Z
        0x20,0x02,0x18,0x02,0x76,0x00 // JR NZ,fail; JR success; HALT(fail); NOP
    };
    // Serial bank bits, least significant first: 0x20000 >> 15 = 4.
    for (unsigned bit = 0; bit < 9; ++bit)
    {
        program.insert(program.end(), {0x3e,static_cast<std::uint8_t>((4 >> bit) & 1),0x32,0x00,0x60});
    }
    program.insert(program.end(), {0x3a,0x00,0x80,0x32,0x01,0x01,0x32,0x01,0x80,0x76});
    memory[0x20000] = 0xe7;
    Program(program);
    Z80::Advance(15 * 10000);
    Require(Z80::Read68k(0x100) == 0xff, "68k read succeeded while Z80 owned its bus");
    OwnBus();
    Require(Z80::Read68k(0x100) == 0x2a, "Z80 program did not write RAM");
    Require(Z80::Read68k(0x2100) == 0x2a, "Z80 RAM mirror failed");
    Require(Z80::Read68k(0x122) == 0x5a, "Indexed Z80 instruction failed");
    Require(Z80::Read68k(0x101) == 0xe7 && memory[0x20001] == 0xe7, "Z80 serial bank register/window failed");
    const auto before = Z80::ExecutedCycles();
    Z80::Advance(15 * 1000);
    Require(Z80::ExecutedCycles() == before, "Z80 executed while its bus was granted to the 68k");
    Z80::SetReset(true);
    Require(Z80::Read68k(0x100) == 0x2a, "Reset incorrectly erased Z80 RAM");

    // IM1 interrupt wakes HALT, executes the RST38 handler and returns with RETI.
    Z80::Init(Read,Write,nullptr);
    Program({0xf3,0x31,0xf0,0x1f,0xed,0x56,0xfb,0x76,0x18,0xfd});
    OwnBus();
    const std::array<std::uint8_t,7> handler{{0x3e,0x77,0x32,0x02,0x01,0xed,0x4d}};
    for (std::size_t i = 0; i < handler.size(); ++i) Z80::Write68k(0x38+i,handler[i]);
    Z80::SetBusRequest(false);
    Z80::Advance(15 * 200);
    Z80::Interrupt(true);
    Z80::Advance(15 * 300);
    Z80::Interrupt(false);
    OwnBus();
    Require(Z80::Read68k(0x102) == 0x77, "Z80 IM1 interrupt/HALT/RETI failed");
}
void TestAudio()
{
    constexpr unsigned clock = 53693175;
    Audio::Init(clock);
    Audio::Advance(clock);
    const auto second = Audio::DrainSamples();
    Require(second.size() == 96000, "Audio did not produce exactly 48000 stereo frames per second");
    Require(Audio::DrainSamples().empty(), "Draining audio did not consume samples");

    Fm(0x24,0xff); Fm(0x25,3); Fm(0x27,0x05); // Timer A period: 144 FM clocks.
    Require((Audio::ReadFM(0) & 0x80) != 0, "YM2612 busy state was not asserted by a write");
    Audio::Advance(7 * 144 - 1);
    Require((Audio::ReadFM(0) & 1) == 0, "YM2612 timer expired too early");
    Audio::Advance(1);
    Require((Audio::ReadFM(0) & 1) != 0, "YM2612 timer did not expire on its clock boundary");
    Audio::Advance(7 * 48);
    Require((Audio::ReadFM(0) & 0x80) == 0, "YM2612 busy state did not expire");
    Fm(0x27,0x10);
    Require((Audio::ReadFM(0) & 1) == 0, "YM2612 timer status reset failed");

    Audio::Init(clock);
    Audio::WritePSG(0x84); Audio::WritePSG(0x0d); // Tone 0 divider 0xD4, about 528 Hz.
    Audio::WritePSG(0x90); // Tone 0 at full volume, all other channels muted.
    Audio::Advance(clock / 10);
    auto tone = Audio::DrainSamples();
    Require(tone.size() == (std::uint64_t(clock / 10) * 48000 / clock) * 2, "Fractional frame sample accounting failed");
    int positive = 0, negative = 0;
    for (std::size_t i = 2000; i < tone.size(); i += 2)
    {
        positive += tone[i] > 1000;
        negative += tone[i] < -1000;
        Require(tone[i] == tone[i+1], "Mono PSG output was not mixed equally into stereo");
    }
    Require(positive > 100 && negative > 100, "PSG tone generator did not oscillate");
    Audio::WritePSG(0x9f);
    Audio::Advance(clock / 5);
    const auto muted = Audio::DrainSamples();
    Require(std::all_of(muted.end()-100,muted.end(),[](std::int16_t v) { return std::abs(int(v)) <= 2; }),
            "PSG mute/DC filter did not settle to silence");

    Audio::Init(clock);
    // DAC goes only to the left output of channel 6.
    Audio::WriteFM(2,0xb6); Audio::WriteFM(3,0x80);
    Fm(0x2b,0x80); Fm(0x2a,0xff);
    Audio::Advance(clock / 100);
    const auto dac = Audio::DrainSamples();
    Require(std::any_of(dac.begin(),dac.end(),[](std::int16_t v) { return std::abs(int(v)) > 1000; }),
            "YM2612 DAC produced no audio");
    long long left = 0, right = 0;
    for (std::size_t i = 0; i < dac.size(); i += 2) { left += std::abs(int(dac[i])); right += std::abs(int(dac[i+1])); }
    Require(left > right * 5, "YM2612 DAC stereo panning failed");

    // Sound output must depend on emulated timestamps, not the host's slice size.
    Audio::Init(clock);
    Audio::WritePSG(0x85); Audio::WritePSG(0x02); Audio::WritePSG(0x90);
    Audio::WritePSG(0xe4); Audio::WritePSG(0xf4);
    Audio::Advance(1000000);
    const auto whole = Audio::DrainSamples();
    Audio::Init(clock);
    Audio::WritePSG(0x85); Audio::WritePSG(0x02); Audio::WritePSG(0x90);
    Audio::WritePSG(0xe4); Audio::WritePSG(0xf4);
    for (unsigned i=0;i<1000;++i) Audio::Advance(1000);
    Require(Audio::DrainSamples() == whole, "Audio changed when host scheduling slices changed");

    Audio::Init(53203424,44100);
    Audio::Advance(53203424);
    Require(Audio::DrainSamples().size() == 88200, "PAL audio sample rate accounting failed");
}
}
int main()
{
    try
    {
        Audio::Init(53693175);
        TestZ80();
        TestAudio();
        std::cout << "Z80 RAM/banking/bus/reset/interrupts and PSG/YM2612 clocks/timers/DAC/stereo passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
