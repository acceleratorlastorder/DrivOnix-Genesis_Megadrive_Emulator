#include "../../Genesis/genesis.hpp"
#include "../../Audio/audio.hpp"
#include "../../Z80/z80.hpp"
#include "../../Genesis/Cartridge/cartridge.hpp"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <fstream>
namespace {
std::vector<CPU_STATE_DEBUG> history(128);
unsigned cursor=0, count=0;
std::size_t romSize=0;
std::string traceFile;
void Observe(const CPU_STATE_DEBUG& state) {
    history[cursor++%history.size()]=state;++count;
    if((state.programCounter&0xffffff)<romSize) return;
    std::ofstream out(traceFile);
    for(unsigned i=0;i<std::min<unsigned>(count,history.size());++i) {
        const auto& c=history[(cursor-std::min<unsigned>(count,history.size())+i)%history.size()];
        out<<std::hex<<"pc="<<c.programCounter<<" op="<<((unsigned(Cartridge::Read(c.programCounter))<<8)|Cartridge::Read(c.programCounter+1))<<" sr="<<c.CCR;
        for(unsigned r=0;r<8;++r) out<<" d"<<r<<"="<<c.registerData[r];
        for(unsigned r=0;r<8;++r) out<<" a"<<r<<"="<<c.registerAddress[r];
        out<<'\n';
    }
    out.close();throw std::runtime_error("PC left cartridge ROM; instruction history saved to "+traceFile);
}
}
int main(int argc,char** argv) {
    if(argc<2) return 2;
    try {
        Genesis::SetRomName(argv[1]);RomLoader::LoadRomFile(argv[1]);
        romSize=RomLoader::Bytes().size();
        Genesis::AllocM68kMemory();Genesis::ResetM68kMemory();Genesis::InsertCartridge();
        Genesis::Init();M68k::Init();YM7101::Init();
        bool bios=false, activeZ80=false;std::string dumpPath;
        for(int i=3;i<argc;++i) {
            const std::string arg=argv[i];
            bios|=arg=="--bios";activeZ80|=arg=="--z80";
            if(arg=="--dump" && i+1<argc) dumpPath=argv[++i];
            if(arg=="--trace-rom" && i+1<argc) {traceFile=argv[++i];Genesis::SetInstructionObserver(Observe);}
        }
        if(activeZ80) {
            Z80::SetBusRequest(true);Z80::Write68k(0,0xc3);Z80::Write68k(1,0);Z80::Write68k(2,0);
            Z80::SetReset(false);Z80::SetBusRequest(false);
            Audio::WritePSG(0x84);Audio::WritePSG(0x20);Audio::WritePSG(0x90);
        }
        const unsigned frames=argc>2 ? unsigned(std::stoul(argv[2])):600;
        uint64_t samples=0;
        const auto start=std::chrono::steady_clock::now();
        for(unsigned i=0;i<frames;++i) {
            if(bios) Genesis::M68KWriteMemoryBYTE(0xff00fe,'U');
            Genesis::RunFrame();samples+=Audio::DrainSamples().size()/2;
            if(i%120==0) {auto c=M68k::GetCpuState();std::cerr<<"frame="<<std::dec<<i<<" pc="<<std::hex<<c.programCounter<<" sr="<<c.CCR<<" sp="<<c.registerAddress[7]<<" magic="<<Genesis::M68KReadMemoryLONG(0xff0100)<<" vdp="<<YM7101::ReadControlPortWORD()<<" d2="<<c.registerData[2]<<" d3="<<c.registerData[3]<<" d4="<<c.registerData[4]<<" a0="<<c.registerAddress[0]<<" a2="<<c.registerAddress[2]<<"\n";}
            if(bios && Genesis::M68KReadMemoryLONG(0xff0100)==0xcafe) {std::cout<<"BIOS completed frame "<<i<<"\n";break;}
        }
        const auto c=M68k::GetCpuState();const auto p=CRT::Pixels()+112*320*4+160*4;
        const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        std::cout<<"frames="<<std::dec<<Genesis::FrameCount()<<" wall_seconds="<<seconds<<" cycles="<<Genesis::MasterCycles()<<" z80_cycles="<<Z80::ExecutedCycles()<<" audio_samples="<<samples<<" pc="<<std::hex<<c.programCounter<<" center="<<unsigned(p[0])<<","<<unsigned(p[1])<<","<<unsigned(p[2])<<"\n";
        if(!dumpPath.empty()) {
            std::ofstream file(dumpPath,std::ios::binary);file<<"P6\n320 "<<CRT::Height()<<"\n255\n";
            const auto bytes=CRT::Pixels();
            for(unsigned i=0;i<320*CRT::Height();++i) file.write(reinterpret_cast<const char*>(bytes+i*4),3);
            if(!file) throw std::runtime_error("Cannot write framebuffer dump");
        }
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
