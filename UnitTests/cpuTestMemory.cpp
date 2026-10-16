#include "cpuTestMemory.hpp"

#include <algorithm>
#include <vector>

namespace
{
    std::vector<byte> memory;

    dword MapAddress(dword address)
    {
        address &= 0xFFFFFF;
        // Match Genesis::M68K*'s current RAM mapping for this isolated fixture.
        return address >= 0xE00000 ? 0xE00000 + (address % 0x100000) : address;
    }
}

void Genesis::AllocM68kMemory()
{
    memory.resize(0x1000000);
}

void Genesis::ResetM68kMemory()
{
    std::fill(memory.begin(), memory.end(), 0);
}

byte Genesis::M68KReadMemoryBYTE(dword address)
{
    return memory.at(MapAddress(address));
}

word Genesis::M68KReadMemoryWORD(dword address)
{
    return (word(M68KReadMemoryBYTE(address)) << 8) | M68KReadMemoryBYTE(address + 1);
}

dword Genesis::M68KReadMemoryLONG(dword address)
{
    return (dword(M68KReadMemoryWORD(address)) << 16) | M68KReadMemoryWORD(address + 2);
}

void Genesis::M68KWriteMemoryBYTE(dword address, byte data)
{
    if ((address & 0xFFFFFF) >= 0xE00000)
        memory.at(MapAddress(address)) = data;
}

void Genesis::M68KWriteMemoryWORD(dword address, word data)
{
    M68KWriteMemoryBYTE(address, byte(data >> 8));
    M68KWriteMemoryBYTE(address + 1, byte(data));
}

void Genesis::M68KWriteMemoryLONG(dword address, dword data)
{
    M68KWriteMemoryWORD(address, word(data >> 16));
    M68KWriteMemoryWORD(address + 2, word(data));
}
