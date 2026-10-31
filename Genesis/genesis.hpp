#ifndef GENESIS_HPP
#define GENESIS_HPP
#include <cstdint>
#include <string>
#include <vector>
#include "RomLoader/romLoader.hpp"
#include "GamePad/gamePad.hpp"
#include "../M68k/m68k.hpp"
#include "../YM7101/ym7101.hpp"
#include "../CathodeRayTube/crt.hpp"
#define M68K_MEM_SIZE 0x1000000
class Genesis {
public:
    Genesis();
    static void Init();
    static void SetRegionToPAL(bool pal);
    static void SetHardwareRegion(byte region);
    static byte GetHardwareRegion();
    static bool IsPAL();
    static uint32_t MasterClock();
    static uint64_t MasterCycles();
    static uint64_t FrameCount();
    static void RunFrame();
    static void SetInstructionObserver(void (*observer)(const CPU_STATE_DEBUG&));
    static void StallMasterCycles(unsigned ticks);
    static void ResetDevices();
    static void ResetM68kMemory();
    static void AllocM68kMemory();
    static void InsertCartridge();
    static void InsertCartridge(const std::vector<byte>& rom, const std::string& path);
    static void SaveCartridge();
    static void Run();
    static bool Bios(byte country);
    static void SetRomName(const std::string& path);
    static const std::string& GetRomName();
    static byte M68KReadMemoryBYTE(dword address);
    static void M68KWriteMemoryBYTE(dword address, byte data);
    static word M68KReadMemoryWORD(dword address);
    static void M68KWriteMemoryWORD(dword address, word data);
    static dword M68KReadMemoryLONG(dword address);
    static void M68KWriteMemoryLONG(dword address, dword data);
private:
    static Genesis& get();
    bool Loop(bool bios, byte country);
    void Advance(unsigned masterCycles);
    std::string romName;
    bool powerOff;
    byte hardwareRegion;
    uint64_t masterCycles, frameDeadline, frames;
    word openBus;
    void (*observer)(const CPU_STATE_DEBUG&);
    byte io[0x20];
    std::vector<byte> ram;
};
#endif
