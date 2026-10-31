#include "z80.hpp"
#include "../Audio/audio.hpp"
#include "core.h"

#include <array>
#include <algorithm>

namespace
{
struct State
{
    md_z80_t cpu{};
    std::uint64_t pins = 0;
    std::uint64_t clockRemainder = 0, executed = 0;
    std::array<std::uint8_t,8192> ram{};
    std::uint32_t bank = 0;
    bool busRequest = false, granted = false, reset = true, interrupt = false, advancing = false;
    Z80::Read68kCallback read68k = nullptr;
    Z80::Write68kCallback write68k = nullptr;
    void* user = nullptr;
};
State machine;

std::uint8_t ReadBus(std::uint32_t address)
{
    std::uint8_t value = machine.read68k ? machine.read68k(address, machine.user) : 0xff;
    // The upper six status bits are unconnected on the Z80-side data bus.
    if ((address & 0xe700fd) == 0xc00004) value |= 0xfc;
    return value;
}

std::uint8_t Read(std::uint16_t address)
{
    if (address < 0x4000) return machine.ram[address & 0x1fff];
    if (address < 0x6000) return Audio::ReadFM(address & 3);
    // This is an alias of the 68000 VDP bus, not the SMS Z80 port protocol.
    // Each byte access has the same data/control side effects as its 68k alias.
    if (address >= 0x7f00 && address < 0x7f10)
        return ReadBus(0xc00000 | (address & 0x0f));
    if (address >= 0x8000)
    {
        const std::uint32_t target = machine.bank | (address & 0x7fff);
        // The Z80 cannot recursively read its own 68k-side window.
        if ((target & 0xff0000) == 0xa00000) return 0xff;
        return ReadBus(target);
    }
    return 0xff;
}
void Write(std::uint16_t address, std::uint8_t value)
{
    if (address < 0x4000) machine.ram[address & 0x1fff] = value;
    else if (address < 0x6000) Audio::WriteFM(address & 3, value);
    else if (address >= 0x6000 && address < 0x6100)
        machine.bank = ((machine.bank >> 1) | (static_cast<std::uint32_t>(value & 1) << 23)) & 0xff8000;
    else if (address >= 0x7f00 && address < 0x7f08)
    {
        if (machine.write68k) machine.write68k(0xc00000 | (address & 7), value, machine.user);
    }
    else if (address >= 0x7f10 && address < 0x7f18 && (address & 1))
        Audio::WritePSG(value);
    else if (address >= 0x8000)
    {
        const std::uint32_t target = machine.bank | (address & 0x7fff);
        if ((target & 0xff0000) != 0xa00000 && machine.write68k) machine.write68k(target, value, machine.user);
    }
}
}

void Z80::Init(Read68kCallback read68k, Write68kCallback write68k, void* user)
{
    machine = State{};
    machine.read68k = read68k;
    machine.write68k = write68k;
    machine.user = user;
    machine.pins = md_z80_init(&machine.cpu);
}
void Z80::Advance(std::uint64_t masterCycles)
{
    // A bus callback may wait for the VDP FIFO by advancing console time. Its
    // pending read/write must complete before this CPU can take another tick.
    // Nested elapsed time is a bus wait: sound keeps running and CPU phase is
    // retained, without re-entering the instruction engine before its data arrives.
    if (machine.advancing)
    {
        Audio::Advance(masterCycles);
        machine.clockRemainder = (machine.clockRemainder + masterCycles % 15) % 15;
        return;
    }
    struct Advancing
    {
        bool& flag;
        explicit Advancing(bool& value) : flag(value) { flag = true; }
        ~Advancing() { flag = false; }
    } advancing(machine.advancing);
    while (masterCycles)
    {
        if (machine.reset || (machine.busRequest && machine.granted))
        {
            machine.granted = machine.busRequest;
            Audio::Advance(masterCycles);
            machine.clockRemainder = (machine.clockRemainder + masterCycles % 15) % 15;
            return;
        }
        const std::uint64_t elapsed = std::min<std::uint64_t>(masterCycles, 15 - machine.clockRemainder);
        Audio::Advance(elapsed);
        masterCycles -= elapsed;
        machine.clockRemainder += elapsed;
        if (machine.clockRemainder != 15) continue;
        machine.clockRemainder = 0;
        if (machine.busRequest && md_z80_opdone(&machine.cpu))
        {
            machine.granted = true;
            continue;
        }
        machine.granted = false;
        if (machine.interrupt) machine.pins |= Z80_INT;
        else machine.pins &= ~Z80_INT;
        machine.pins = md_z80_tick(&machine.cpu, machine.pins);
        ++machine.executed;
        const auto address = Z80_GET_ADDR(machine.pins);
        if (machine.pins & Z80_MREQ)
        {
            if (machine.pins & Z80_RD) { Z80_SET_DATA(machine.pins, Read(address)); }
            else if (machine.pins & Z80_WR) Write(address, Z80_GET_DATA(machine.pins));
        }
        else if (machine.pins & Z80_IORQ)
        {
            // Genesis software uses memory-mapped devices; interrupt acknowledge
            // is unvectored and returns the floating data bus value.
            if (machine.pins & (Z80_RD | Z80_M1)) { Z80_SET_DATA(machine.pins, 0xff); }
        }
    }
}
void Z80::SetBusRequest(bool asserted)
{
    machine.busRequest = asserted;
    if (!asserted) machine.granted = false;
    else if (machine.reset) machine.granted = true;
}
void Z80::SetReset(bool asserted)
{
    if (asserted && !machine.reset)
    {
        machine.pins = md_z80_reset(&machine.cpu);
        machine.bank = 0;
    }
    machine.reset = asserted;
    machine.granted = machine.busRequest && asserted;
}
bool Z80::BusGranted() { return machine.granted; }
std::uint8_t Z80::Read68k(std::uint32_t offset)
{
    return machine.granted ? Read(static_cast<std::uint16_t>(offset)) : 0xff;
}
void Z80::Write68k(std::uint32_t offset, std::uint8_t value)
{
    if (machine.granted) Write(static_cast<std::uint16_t>(offset), value);
}
void Z80::Interrupt(bool asserted) { machine.interrupt = asserted; }
std::uint64_t Z80::ExecutedCycles() { return machine.executed; }
