#include "cputestmemory.hpp"

#include <algorithm>
#include <vector>

namespace
{
    std::vector<byte> memory;
    bool flatBus = false;
    bool observeWrites = false;
    std::vector<unsigned char> dirty(0x10000);
    std::vector<dword> dirtyPages;
    std::vector<dword> written;

    dword MapAddress(dword address)
    {
        address &= 0xFFFFFF;
        if (flatBus) return address;
        // Match Genesis::M68K*'s current RAM mapping for this isolated fixture.
        return address >= 0xE00000 ? 0xE00000 + (address % 0x100000) : address;
    }

    void Store(dword address, byte value)
    {
        address = MapAddress(address);
        const dword page = address >> 8;
        if (!dirty[page])
        {
            dirty[page] = 1;
            dirtyPages.push_back(page);
        }
        memory.at(address) = value;
        if (observeWrites) written.push_back(address);
    }
}

void CpuTestMemory::SetFlatBus(bool enabled) { flatBus = enabled; }
void CpuTestMemory::SeedByte(dword address, byte value) { Store(address, value); }
void CpuTestMemory::ObserveWrites() { written.clear(); observeWrites = true; }
const std::vector<dword>& CpuTestMemory::WrittenAddresses() { return written; }

void Genesis::AllocM68kMemory()
{
    memory.resize(0x1000000);
}

void Genesis::ResetM68kMemory()
{
    for (dword page : dirtyPages)
    {
        std::fill(memory.begin() + (page << 8), memory.begin() + ((page + 1) << 8), 0);
        dirty[page] = 0;
    }
    dirtyPages.clear();
    written.clear();
    observeWrites = false;
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
    if (flatBus || (address & 0xFFFFFF) >= 0xE00000)
        Store(address, data);
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
