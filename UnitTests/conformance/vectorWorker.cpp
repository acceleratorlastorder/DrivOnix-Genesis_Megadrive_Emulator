#include "../../M68k/m68k.hpp"
#include "../cpuTestMemory.hpp"

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace
{
    bool Read32(dword& value, bool allowEof = false)
    {
        unsigned char bytes[4];
        const size_t count = std::fread(bytes, 1, 4, stdin);
        if (count == 0 && allowEof && std::feof(stdin)) return false;
        if (count != 4) throw std::runtime_error("truncated request");
        value = dword(bytes[0]) | (dword(bytes[1]) << 8) |
                (dword(bytes[2]) << 16) | (dword(bytes[3]) << 24);
        return true;
    }

    dword Read32()
    {
        dword value;
        Read32(value);
        return value;
    }

    void Write32(FILE* stream, dword value)
    {
        unsigned char bytes[4] = {byte(value), byte(value >> 8), byte(value >> 16), byte(value >> 24)};
        if (std::fwrite(bytes, 1, 4, stream) != 4) throw std::runtime_error("response write failed");
    }

    FILE* OpenProtocol()
    {
        // Keep CPU printf/cout diagnostics out of the framed binary response.
        std::fflush(stdout);
#ifdef _WIN32
        _setmode(_fileno(stdin), _O_BINARY);
        FILE* protocol = _fdopen(_dup(_fileno(stdout)), "wb");
        if (protocol) _setmode(_fileno(protocol), _O_BINARY);
        const char* sink = "NUL";
#else
        FILE* protocol = fdopen(dup(fileno(stdout)), "wb");
        const char* sink = "/dev/null";
#endif
        if (!protocol || !std::freopen(sink, "w", stdout)) throw std::runtime_error("cannot open protocol");
        return protocol;
    }
}

int main()
{
    try
    {
        FILE* protocol = OpenProtocol();
        Genesis::AllocM68kMemory();
        CpuTestMemory::SetFlatBus(true);
        dword magic;
        while (Read32(magic, true))
        {
            if (magic != 0x31515644) throw std::runtime_error("invalid request magic/version");
            const dword id = Read32();
            const dword dispatch = Read32();
            if (dispatch > 1) throw std::runtime_error("invalid dispatch");
            CPU_VECTOR_STATE initial = {};
            initial.cpu.programCounter = Read32();
            initial.cpu.CCR = word(Read32());
            for (int i = 0; i < 8; ++i) initial.cpu.registerData[i] = Read32();
            for (int i = 0; i < 7; ++i) initial.cpu.registerAddress[i] = Read32();
            initial.usp = Read32();
            initial.ssp = Read32();
            Genesis::ResetM68kMemory();
            const dword ramCount = Read32();
            if (ramCount > 0x10000) throw std::runtime_error("oversized RAM seed");
            for (dword i = 0; i < ramCount; ++i)
            {
                const dword address = Read32();
                const dword value = Read32();
                if (address > 0xFFFFFF || value > 255) throw std::runtime_error("invalid RAM seed");
                CpuTestMemory::SeedByte(address, byte(value));
            }
            const dword checkCount = Read32();
            if (checkCount > 0x10000) throw std::runtime_error("oversized observation list");
            std::vector<dword> addresses;
            for (dword i = 0; i < checkCount; ++i)
            {
                const dword address = Read32();
                if (address > 0xFFFFFF) throw std::runtime_error("invalid observation address");
                addresses.push_back(address);
            }
            M68k::Init();
            M68k::LoadVectorState(initial);
            CpuTestMemory::ObserveWrites();
            const int cycles = M68k::StepVector(dispatch == 1);
            const CPU_VECTOR_STATE final = M68k::ReadVectorState();
            const std::vector<dword>& writes = CpuTestMemory::WrittenAddresses();
            addresses.insert(addresses.end(), writes.begin(), writes.end());
            std::sort(addresses.begin(), addresses.end());
            addresses.erase(std::unique(addresses.begin(), addresses.end()), addresses.end());

            Write32(protocol, 0x31525644);
            Write32(protocol, id);
            Write32(protocol, dword(cycles));
            Write32(protocol, final.cpu.programCounter);
            Write32(protocol, final.cpu.CCR);
            Write32(protocol, final.cpu.stop);
            for (int i = 0; i < 8; ++i) Write32(protocol, final.cpu.registerData[i]);
            for (int i = 0; i < 8; ++i) Write32(protocol, final.cpu.registerAddress[i]);
            Write32(protocol, final.usp);
            Write32(protocol, final.ssp);
            Write32(protocol, dword(addresses.size()));
            for (dword address : addresses)
            {
                Write32(protocol, address);
                Write32(protocol, Genesis::M68KReadMemoryBYTE(address));
            }
            if (std::fflush(protocol) != 0) throw std::runtime_error("response flush failed");
        }
        std::fclose(protocol);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "vector adapter: " << error.what() << std::endl;
        return 2;
    }
}
