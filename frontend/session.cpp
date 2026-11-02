#include "session.hpp"
#include "../genesis/genesis.hpp"
#include "../platform/storage.hpp"
#ifndef DRIVONIX_HEADLESS
#include "../platform/audiooutput.hpp"
#endif
#include <stdexcept>
#include <utility>

namespace Frontend {
PreparedGame Session::Prepare(const std::string& path,const std::string& member,bool showBios,
                              const std::string& biosPath,const Process::CancelToken& cancel) {
    PreparedGame next;next.path=path;next.member=member;next.showBios=showBios;
    next.rom=RomInfo::Load(path,member,cancel);
    next.cartridge=Cartridge::Prepare(next.rom.bytes,next.rom.saveIdentity);
    const unsigned clock=next.rom.region=='E'?53203424:53693175;
    next.audio=Audio::Prepare(clock);
    if(showBios) {
        const auto bytes=Storage::ReadBytes(biosPath,0x800000,cancel);
        if(bytes.size()<0x200) throw std::runtime_error("The BIOS file is incomplete");
        next.bios=Cartridge::Prepare(bytes,biosPath);next.biosAudio=Audio::Prepare(clock);
    }
    return next;
}
void Session::ResetMachine(Cartridge::Prepared cartridge,std::shared_ptr<Audio::PreparedState> audio) {
    Genesis::ResetM68kMemory();Cartridge::Commit(std::move(cartridge));
    Genesis::SetHardwareRegion(current_.rom.region);Genesis::InitPrepared(std::move(audio));
    GamePad::Init();M68k::Init();YM7101::Init();CRT::ResetScreen();
}
void Session::Start(PreparedGame prepared) {
    Pause();
    // Validation/allocation takes place before changing the running machine.
    Genesis::AllocM68kMemory();
    Cartridge::Save();
    // Refresh after saving: restarting or selecting an alias of the active ROM
    // must load its latest battery RAM rather than the earlier staged bytes.
    prepared.cartridge=Cartridge::Prepare(prepared.rom.bytes,prepared.rom.saveIdentity);
    Genesis::SetRomName(prepared.rom.saveIdentity);
    current_=std::move(prepared);bios_=current_.showBios;
    if(bios_) ResetMachine(std::move(current_.bios),std::move(current_.biosAudio));
    else CommitGame();
    active_=true;paused_=false;activeFrames_=0;
#ifndef DRIVONIX_HEADLESS
    if(hostAudio_)AudioOutput::Resume();
#endif
}
void Session::CommitGame() {
    ResetMachine(std::move(current_.cartridge),std::move(current_.audio));bios_=false;
}
void Session::Pause() {
    paused_=true;
    for(int p=0;p<2;++p)GamePad::SetInputState(p,0);
#ifndef DRIVONIX_HEADLESS
    if(hostAudio_)AudioOutput::Suspend();
#endif
}
void Session::Resume() {
    if(!active_)return;paused_=false;
#ifndef DRIVONIX_HEADLESS
    if(hostAudio_)AudioOutput::Resume();
#endif
}
void Session::Restart() {
    if(!active_)return;
    Pause();Cartridge::Save();
    PreparedGame next;next.path=current_.path;next.member=current_.member;next.rom=current_.rom;
    next.cartridge=Cartridge::Prepare(next.rom.bytes,next.rom.saveIdentity);
    next.audio=Audio::Prepare(next.rom.region=='E'?53203424:53693175);
    // Restart is the cartridge's full reset, preserving its battery memory.
    Start(std::move(next));
}
void Session::Stop() {Pause();Cartridge::Save();active_=false;bios_=false;}
void Session::SkipBios() {if(active_&&bios_)CommitGame();}
void Session::Step(const Controller::Pads& buttons) {
    if(!active_||paused_)return;
    for(int p=0;p<2;++p)GamePad::SetInputState(p,buttons[p]);
    if(bios_)Genesis::M68KWriteMemoryBYTE(0xff00fe,current_.rom.region);
    Genesis::RunFrame();
    auto samples=Audio::DrainSamples();
#ifndef DRIVONIX_HEADLESS
    if(hostAudio_)AudioOutput::Submit(samples);
#endif
    if(bios_&&Genesis::M68KReadMemoryLONG(0xff0100)==0xcafe)CommitGame();
    else if(!bios_)++activeFrames_;
}
double Session::FrameSeconds() const {return double(3420*(Genesis::IsPAL()?313:262))/Genesis::MasterClock();}
}
