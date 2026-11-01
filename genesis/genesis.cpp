#include "genesis.hpp"
#include "cartridge/cartridge.hpp"
#include "cartridge/eeprom.hpp"
#include "../z80/z80.hpp"
#include "../audio/audio.hpp"
#ifndef DRIVONIX_HEADLESS
#include "../platform/audiooutput.hpp"
#include <chrono>
#include <thread>
#endif
#include <algorithm>
#include <cstring>
#include <stdexcept>
namespace {
byte ZRead(dword address, void*) { return Genesis::M68KReadMemoryBYTE(address); }
void ZWrite(dword address, byte data, void*) { Genesis::M68KWriteMemoryBYTE(address,data); }
bool VDPAddress(dword a) { return (a&0xe700e0)==0xc00000; }
}
Genesis::Genesis() : powerOff(false),hardwareRegion('U'),masterCycles(0),frameDeadline(0),frames(0),openBus(0xffff),observer(nullptr),ram(65536,0) { std::memset(io,0,sizeof(io)); }
Genesis& Genesis::get() { static Genesis instance; return instance; }
void Genesis::Init() {
    auto& s=get();
    std::memset(s.io,0,sizeof(s.io));
    for (unsigned i=0xf;i<0x20;i+=6) s.io[i]=0xff;
    s.masterCycles=s.frameDeadline=s.frames=0;
    s.openBus=0xffff; s.powerOff=false;
    Audio::Init(MasterClock());
    EEPROM::SetMasterClock(MasterClock());
    Z80::Init(ZRead,ZWrite,nullptr);
    GamePad::Init();
    M68k::SetResetHandler(ResetDevices);
}
void Genesis::ResetDevices() {
    Z80::SetReset(true);Z80::SetBusRequest(false);
    Audio::Init(MasterClock());
    EEPROM::SetMasterClock(MasterClock());
    GamePad::Init();
    YM7101::Init();
}
void Genesis::SetRegionToPAL(bool pal) { SetHardwareRegion(pal ? 'E':'U'); }
void Genesis::SetHardwareRegion(byte region) { get().hardwareRegion=region=='J' || region=='E' ? region:'U'; }
byte Genesis::GetHardwareRegion() { return get().hardwareRegion; }
bool Genesis::IsPAL() { return GetHardwareRegion()=='E'; }
uint32_t Genesis::MasterClock() { return IsPAL() ? 53203424:53693175; }
uint64_t Genesis::MasterCycles() { return get().masterCycles; }
uint64_t Genesis::FrameCount() { return get().frames; }
void Genesis::AllocM68kMemory() { get().ram.resize(65536); }
void Genesis::ResetM68kMemory() { std::fill(get().ram.begin(),get().ram.end(),0); }
void Genesis::InsertCartridge() { Cartridge::Load(RomLoader::Bytes(),GetRomName());EEPROM::SetMasterClock(MasterClock()); }
void Genesis::InsertCartridge(const std::vector<byte>& rom,const std::string& path) { Cartridge::Load(rom,path);EEPROM::SetMasterClock(MasterClock()); }
void Genesis::SaveCartridge() { Cartridge::Save(); }
void Genesis::SetRomName(const std::string& path) { get().romName=path; }
const std::string& Genesis::GetRomName() { return get().romName; }
void Genesis::Advance(unsigned ticks) {
    masterCycles+=ticks;
    YM7101::Advance(ticks);
    Z80::Advance(ticks);
    GamePad::Advance(ticks);
    EEPROM::Advance(ticks);
    if (YM7101::GetRequestInt()) {
        const int level=YM7101::GetIntType();
        if (level>=0) M68k::RequestAutoVectorInt(static_cast<M68k::INT_TYPE>(level));
    }
    Z80::Interrupt(YM7101::Z80Interrupt());
}
void Genesis::SetInstructionObserver(void (*observer)(const CPU_STATE_DEBUG&)) { get().observer=observer; }
void Genesis::StallMasterCycles(unsigned ticks) { if (ticks) get().Advance(ticks); }
void Genesis::RunFrame() {
    auto& s=get();
    s.frameDeadline+=uint64_t(3420)*(IsPAL() ? 313:262);
    while (s.masterCycles<s.frameDeadline) {
        const unsigned stall=YM7101::TakeStallCycles();
        if (stall) { s.Advance(stall); continue; }
        if(s.observer) s.observer(M68k::GetCpuState());
        const int cycles=M68k::Update();
        if (cycles<0) throw std::runtime_error("68000 returned a negative cycle count");
        // STOP still allows video, audio and interrupt sources to advance.
        s.Advance(unsigned(cycles>0 ? cycles:4)*7);
    }
    ++s.frames;
}
bool Genesis::Loop(bool bios,byte country) {
#ifndef DRIVONIX_HEADLESS
    auto& s=get();
    using Clock=std::chrono::steady_clock;
    auto origin=Clock::now();
    uint64_t wallFrames=0;
    const double period=double(3420*(IsPAL()?313:262))/MasterClock();
    s.powerOff=false;
    while (sfRenderWindow_isOpen(CRT::GetWindow()) && !s.powerOff) {
        sfEvent event;
        while (sfRenderWindow_pollEvent(CRT::GetWindow(),&event)) {
            if (event.type==sfEvtClosed) sfRenderWindow_close(CRT::GetWindow());
            s.powerOff=s.powerOff || GamePad::Update(event);
        }
        if (!sfRenderWindow_isOpen(CRT::GetWindow()) || s.powerOff) break;
        GamePad::PollControllers(sfRenderWindow_hasFocus(CRT::GetWindow()) != sfFalse);
        if (bios) M68KWriteMemoryBYTE(0xff00fe,country);
        RunFrame();
        AudioOutput::Submit(Audio::DrainSamples());
        CRT::Render();
        if (bios && M68KReadMemoryLONG(0xff0100)==0xcafe) return true;
        ++wallFrames;
        auto deadline=origin+std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(wallFrames*period));
        if (Clock::now()>deadline+std::chrono::milliseconds(100)) { origin=Clock::now();wallFrames=0; }
        else std::this_thread::sleep_until(deadline);
    }
#else
    (void)bios; (void)country;
#endif
    return false;
}
void Genesis::Run() { get().Loop(false,0); SaveCartridge(); }
bool Genesis::Bios(byte country) { return get().Loop(true,country); }
byte Genesis::M68KReadMemoryBYTE(dword a) {
    a&=0xffffff;
    auto& s=get();
    if (a<0x400000) return Cartridge::Read(a);
    if (a>=0xe00000) return s.ram[a&0xffff];
    if (a>=0xa00000 && a<=0xa0ffff) return Z80::Read68k(a&0xffff);
    if (a==0xa11100) return Z80::BusGranted()?0:1;
    if (a>=0xa10000 && a<0xa10020) {
        switch (a|1) {
        case 0xa10001: return byte((GetHardwareRegion()=='J' ? 0:0x80)|(IsPAL()?0x40:0)|0x20);
        case 0xa10003: return GamePad::ReadData(0);
        case 0xa10005: return GamePad::ReadData(1);
        case 0xa10007: return 0x7f;
        case 0xa10009: return GamePad::ReadControl(0);
        case 0xa1000b: return GamePad::ReadControl(1);
        default: return s.io[a&0x1f];
        }
    }
    if (VDPAddress(a)) {
        const word value=M68KReadMemoryWORD(a&~1u);
        return (a&1) ? byte(value):byte(value>>8);
    }
    return (a&1) ? byte(s.openBus):byte(s.openBus>>8);
}
void Genesis::M68KWriteMemoryBYTE(dword a,byte value) {
    a&=0xffffff;
    auto& s=get();
    if (a<0x400000 || (a>=0xa130f1 && a<=0xa130ff)) { Cartridge::Write(a,value);return; }
    if (a>=0xe00000) { s.ram[a&0xffff]=value;return; }
    if (a>=0xa00000 && a<=0xa0ffff) { Z80::Write68k(a&0xffff,value);return; }
    if (a==0xa11100) { Z80::SetBusRequest((value&1)!=0);return; }
    if (a==0xa11200) { Z80::SetReset((value&1)==0);return; }
    if (a>=0xa10000 && a<0xa10020) {
        if (!(a&1)) return;
        switch(a) {
        case 0xa10003: GamePad::Write(0,value,false);break;
        case 0xa10005: GamePad::Write(1,value,false);break;
        case 0xa10009: GamePad::Write(0,value,true);break;
        case 0xa1000b: GamePad::Write(1,value,true);break;
        default: s.io[a&0x1f]=value;break;
        }
        return;
    }
    if (VDPAddress(a)) {
        switch(a&0x1f) {
        case 0:case 1:case 2:case 3: YM7101::WriteDataPortBYTE(value);break;
        case 4:case 5:case 6:case 7: YM7101::WriteControlPortBYTE(value);break;
        case 0x11:case 0x13:case 0x15:case 0x17: Audio::WritePSG(value);break;
        }
    }
}
word Genesis::M68KReadMemoryWORD(dword a) {
    a&=0xffffff;
    auto& s=get();
    word value;
    if (VDPAddress(a)) {
        switch(a&0x1e) {
        case 0:case 2: value=YM7101::ReadDataPortWORD();break;
        case 4:case 6: value=word((YM7101::ReadControlPortWORD()&0x03ff)|(s.openBus&0xfc00));break;
        case 8:case 10:case 12:case 14: value=YM7101::GetHVCounter();break;
        default: value=s.openBus;break;
        }
    } else if (a==0xa11100) value=Z80::BusGranted()?0:0x0100;
    else if (a>=0xa00000 && a<=0xa0ffff) {const byte data=Z80::Read68k(a&0xffff);value=word((word(data)<<8)|data);}
    else if (a>=0xa10000 && a<0xa10020) {const byte data=M68KReadMemoryBYTE(a|1);value=word((word(data)<<8)|data);}
    else value=word((word(M68KReadMemoryBYTE(a))<<8)|M68KReadMemoryBYTE((a+1)&0xffffff));
    s.openBus=value;
    return value;
}
void Genesis::M68KWriteMemoryWORD(dword a,word value) {
    a&=0xffffff; get().openBus=value;
    if (a<0x400000) {Cartridge::WriteWord(a,value);return;}
    if (a>=0xa00000 && a<=0xa0ffff) {Z80::Write68k(a&0xffff,byte(value>>8));return;}
    if (VDPAddress(a)) {
        switch(a&0x1e) {
        case 0:case 2: YM7101::WriteDataPortWORD(value);break;
        case 4:case 6: YM7101::WriteControlPortWORD(value);break;
        case 0x10:case 0x12:case 0x14:case 0x16: Audio::WritePSG(byte(value));break;
        }
    } else {
        M68KWriteMemoryBYTE(a,byte(value>>8));
        M68KWriteMemoryBYTE((a+1)&0xffffff,byte(value));
    }
}
dword Genesis::M68KReadMemoryLONG(dword a) { return (dword(M68KReadMemoryWORD(a))<<16)|M68KReadMemoryWORD((a+2)&0xffffff); }
void Genesis::M68KWriteMemoryLONG(dword a,dword value) { M68KWriteMemoryWORD(a,word(value>>16));M68KWriteMemoryWORD((a+2)&0xffffff,word(value)); }
