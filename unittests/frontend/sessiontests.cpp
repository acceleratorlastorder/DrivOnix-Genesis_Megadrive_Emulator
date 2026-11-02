#include "../../frontend/session.hpp"
#include "../../genesis/genesis.hpp"
#include "../../platform/storage.hpp"
#include <iostream>
#include <stdexcept>
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace {
void Need(bool okay,const char* message){if(!okay)throw std::runtime_error(message);}
template<class F> void Fails(F operation){bool failed=false;try{operation();}catch(const std::exception&){failed=true;}Need(failed,"Expected staged operation to fail");}
void Put(std::vector<byte>& rom,unsigned at,unsigned value){rom[at]=byte(value>>24);rom[at+1]=byte(value>>16);rom[at+2]=byte(value>>8);rom[at+3]=byte(value);}
std::vector<byte> Image(char region){
    std::vector<byte> rom(0x4000);Put(rom,0,0xffff00);Put(rom,4,0x200);
    const std::string title="DRIVONIX SESSION TEST";
    std::copy(title.begin(),title.end(),rom.begin()+0x150);rom[0x1f0]=byte(region);
    rom[0x1b0]='R';rom[0x1b1]='A';Put(rom,0x1b4,0x200001);Put(rom,0x1b8,0x2003ff);
    const byte code[]={0x46,0xfc,0x27,0x00,0x13,0xfc,0x00,0x42,0x00,0xff,0x00,0x00,0x60,0xfe};
    std::copy(std::begin(code),std::end(code),rom.begin()+0x200);return rom;
}
}
int main(int argc,char** argv){try{
    Need(argc==3,"Expected fixture directory and BIOS path");
    const auto base=Storage::Join(argv[1],"session");Storage::EnsureDirectory(base);
    const auto path=Storage::Join(base,"first.bin"),pal=Storage::Join(base,"pal.bin");
    Storage::RemoveFile(path+".srm");Storage::RemoveFile(pal+".srm");
    Storage::AtomicWrite(path,Image('U'));Storage::AtomicWrite(pal,Image('E'));
    Frontend::Session session(false);
    auto ready=Frontend::Session::Prepare(path,"",false,"missing-bios");
    session.Start(std::move(ready));Need(session.Active()&&!session.Paused()&&!session.Booting(),"Direct game launch did not start");
    session.Step({{0,0}});Need(Genesis::M68KReadMemoryBYTE(0xff0000)==0x42,"Session did not execute the cartridge");
    Cartridge::Write(0x200001,0x3c);session.Pause();
    const auto cycles=Genesis::MasterCycles(),frames=Genesis::FrameCount();const auto pc=M68k::GetCpuState().programCounter;
    for(int i=0;i<40;++i)session.Step({{0xfff,0xfff}});
    Need(Genesis::MasterCycles()==cycles&&Genesis::FrameCount()==frames&&M68k::GetCpuState().programCounter==pc,"Paused menu advanced the machine");
    Fails([&]{Frontend::Session::Prepare(Storage::Join(base,"missing.bin"),"",false,"");});
    Need(session.Active()&&session.Paused()&&Cartridge::Read(0x200001)==0x3c,"Preparation failure changed active game");
    Storage::AtomicWrite(pal+".srm",{1});
    Fails([&]{Frontend::Session::Prepare(pal,"",false,"");});
    Need(Genesis::MasterCycles()==cycles&&Cartridge::Read(0x200001)==0x3c,"Invalid new save damaged the old game");
    Storage::RemoveFile(pal+".srm");
#ifndef _WIN32
    if(geteuid()!=0){
        auto next=Frontend::Session::Prepare(pal,"",false,"");
        Need(chmod(base.c_str(),0500)==0,"Cannot set save failure fixture");
        Fails([&]{session.Start(std::move(next));});
        Need(chmod(base.c_str(),0700)==0,"Cannot restore save fixture permissions");
        Need(session.Path()==path&&session.Paused()&&Genesis::MasterCycles()==cycles&&Cartridge::Read(0x200001)==0x3c,"Save failure replaced the paused session");
    }
#endif
    session.Restart();Need(Genesis::FrameCount()==0&&M68k::GetCpuState().programCounter==0x200,"Restart did not perform a complete reboot");
    Need(Cartridge::Read(0x200001)==0x3c,"Restart used an out-of-date staged battery save");
    session.Pause();session.Resume();session.Step({{0,0}});Need(Genesis::FrameCount()==1,"Resume tried to catch up menu time");
    auto next=Frontend::Session::Prepare(pal,"",false,"");session.Start(std::move(next));session.Step({{0,0}});
    Need(Genesis::IsPAL()&&Genesis::FrameCount()==1&&Genesis::MasterCycles()>=3420u*313u,"PAL timing was not preserved");
    auto boot=Frontend::Session::Prepare(path,"",true,argv[2]);session.Start(std::move(boot));
    Need(session.Booting(),"Enabled BIOS was not staged");session.Step({{0,0}});session.SkipBios();
    Need(!session.Booting()&&M68k::GetCpuState().programCounter==0x200&&Cartridge::Read(0x200001)==0x3c,"Skipping BIOS damaged cartridge or saved RAM");
    session.Stop();Need(!session.Active(),"Stop did not return to library state");
    const auto saved=Storage::ReadBytes(path+".srm",1024);Need(saved.size()==512&&saved[0]==0x3c,"Session did not persist its battery memory");
    std::cout<<"Session pause/reset/PAL/BIOS staging and failed load/save preservation passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
