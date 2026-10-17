#ifndef CPU_TEST_MEMORY_HPP
#define CPU_TEST_MEMORY_HPP

#include "../Bits/bitsUtils.hpp"
#include <vector>

namespace CpuTestMemory
{
    void SetFlatBus(bool enabled);
    void SeedByte(dword address, byte value);
    void ObserveWrites();
    const std::vector<dword>& WrittenAddresses();
}

// CPU unit-test fixture. This target does not link the console's display or I/O.
class Genesis
{
public:
    static void AllocM68kMemory();
    static void ResetM68kMemory();
    static byte M68KReadMemoryBYTE(dword address);
    static word M68KReadMemoryWORD(dword address);
    static dword M68KReadMemoryLONG(dword address);
    static void M68KWriteMemoryBYTE(dword address, byte data);
    static void M68KWriteMemoryWORD(dword address, word data);
    static void M68KWriteMemoryLONG(dword address, dword data);
};

#endif
