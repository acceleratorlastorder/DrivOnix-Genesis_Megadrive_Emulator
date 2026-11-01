#include "../../genesis/genesis.hpp"
#include "../../genesis/cartridge/cartridge.hpp"
#include "../../audio/audio.hpp"
#include "../../z80/z80.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace {
void Need(bool pass,const char* why) { if(!pass) throw std::runtime_error(why); }
void Register(unsigned reg,unsigned value) { YM7101::WriteControlPortWORD(word(0x8000|(reg<<8)|value)); }
void Command(unsigned address,unsigned code) {
    YM7101::WriteControlPortWORD(word((address&0x3fff)|((code&3)<<14)));
    YM7101::WriteControlPortWORD(word(((address>>14)&3)|((code&0x3c)<<2)));
}
void BE32(std::vector<byte>& data,unsigned at,unsigned value) { for(unsigned i=0;i<4;++i) data[at+i]=byte(value>>(24-i*8)); }
}
int main(int argc,char* argv[]) {
    try {
        Need(argc==2,"Expected fixture directory");
        Genesis::SetRegionToPAL(false);Genesis::AllocM68kMemory();Genesis::ResetM68kMemory();Genesis::Init();YM7101::Init();
        for(byte region : {'J','U','E'}) {
            Genesis::SetHardwareRegion(region);Genesis::Init();YM7101::Init();
            const byte expected=region=='J' ? 0x20:region=='U' ? 0xa0:0xe0;
            Need(Genesis::M68KReadMemoryBYTE(0xa10001)==expected,"ASIC domestic/overseas and PAL/NTSC version byte");
            Need(Genesis::M68KReadMemoryWORD(0xa10000)==word(expected*0x101),"ASIC hardware region word lanes");
            Need(Genesis::MasterClock()==(region=='E' ? 53203424u:53693175u),"Hardware region master clock");
        }
        Genesis::SetHardwareRegion(0xff);Need(Genesis::GetHardwareRegion()=='U',"Invalid hardware region must default to USA");
        Genesis::SetRegionToPAL(false);Genesis::Init();YM7101::Init();
        // A preceding RAM read supplies bits10..15; VDP status only drives bits0..9.
        Genesis::M68KWriteMemoryWORD(0xff0200,0xa5c3);
        Genesis::M68KReadMemoryWORD(0xff0200);
        Need(Genesis::M68KReadMemoryWORD(0xc00004)==0xa600,"VDP status did not merge upper bits from the 68000 bus");
        Genesis::M68KWriteMemoryWORD(0xff0200,0x59ff);
        Genesis::M68KReadMemoryWORD(0xff0200);
        Need(Genesis::M68KReadMemoryWORD(0xc00006)==0x5a00,"VDP status mirror retained a fixed upper-bit value");
        Genesis::M68KWriteMemoryWORD(0xff0200,0);
        Genesis::M68KReadMemoryWORD(0xff0200);
        Need(Genesis::M68KReadMemoryBYTE(0xc00004)==2,"VDP high-byte read must select merged status bits");
        Need(Genesis::M68KReadMemoryBYTE(0xc00005)==0,"VDP low-byte status read mismatch");
        Genesis::Init();YM7101::Init();
        // The I/O ASIC and Z80 expose an 8-bit value on both 68000 read lanes.
        // SGDK cold-start tests these controls with TST.L; opcode/open-bus bits
        // must not make it mistake a cold power-on for a warm reset.
        Need(Genesis::M68KReadMemoryLONG(0xa10008)==0,"Cold-reset controller controls must read zero");
        Genesis::M68KWriteMemoryWORD(0xa10008,0x0040);
        Need(Genesis::M68KReadMemoryWORD(0xa10008)==0x4040,"I/O ASIC duplicated word read lanes");
        Genesis::M68KWriteMemoryWORD(0xa10008,0);
        Z80::SetBusRequest(true);Z80::Write68k(0,0x12);Z80::Write68k(1,0x34);
        Need(Genesis::M68KReadMemoryWORD(0xa00000)==0x1212,"Z80 duplicated word read lanes");
        Genesis::M68KWriteMemoryWORD(0xa00000,0xabcd);
        Need(Z80::Read68k(0)==0xab && Z80::Read68k(1)==0x34,"Z80 word write uses upper byte only");
        Z80::SetBusRequest(false);
        Genesis::M68KWriteMemoryWORD(0xff1234,0xabcd);
        Need(Genesis::M68KReadMemoryWORD(0xe01234)==0xabcd,"RAM alias must repeat every64KiB");
        Genesis::M68KWriteMemoryLONG(0xfffffe,0x12345678);
        Need(Genesis::M68KReadMemoryWORD(0xe0fffe)==0x1234,"RAM end write failed");
        Genesis::M68KWriteMemoryBYTE(0xe00000,0x77);
        Need(Genesis::M68KReadMemoryBYTE(0xff0000)==0x77,"RAM mirror base failed");
        Register(15,2); Command(0xffff,1);YM7101::WriteDataPortWORD(0xabcd);
        Command(0xfffe,0); Need(YM7101::ReadDataPortWORD()==0xcdab,"VRAM odd address lane/wrap failed");
        Register(7,1);Register(0,4);Command(2,3);YM7101::WriteDataPortWORD(0x000e);
        YM7101::Advance(3420);
        const auto pixels=CRT::Pixels();Need(pixels[(32+10)*4]==224,"Backdrop pixel mismatch");
        YM7101::Init();YM7101::Advance(3420*261+3419);Need(YM7101::Scanline()==261,"NTSC scanline count");
        YM7101::Advance(1);Need(YM7101::Scanline()==0,"NTSC frame must be262lines");
        Genesis::SetRegionToPAL(true);YM7101::Init();Register(1,8);
        Need(CRT::Height()==240,"PAL240line mode");
        YM7101::Advance(3420*313);Need(YM7101::Scanline()==0,"PALframe must be313lines");
        // A known tile and a positive horizontal scroll have independent pixel expectations.
        YM7101::Init();Register(15,2);Register(0,4);Register(1,0x40);Register(2,0x30);Register(4,7);
        Register(5,0x6c);Register(7,2);Register(12,0x81);Register(13,0x3f);Register(16,1);
        Command(2,3);YM7101::WriteDataPortWORD(0x000e);YM7101::WriteDataPortWORD(0x00e0);
        Command(32,1);for(unsigned i=0;i<16;++i) YM7101::WriteDataPortWORD(0x1111);
        Command(0xc000,1);YM7101::WriteDataPortWORD(1);YM7101::Advance(3420*262*2);
        Need(CRT::Pixels()[(2*320+2)*4]==224 && CRT::Pixels()[(2*320+10)*4+1]==224,"Tile/background rendering");
        Command(0xfc00,1);YM7101::WriteDataPortWORD(8);YM7101::WriteDataPortWORD(0);YM7101::Advance(3420*262);
        Need(CRT::Pixels()[(2*320+2)*4+1]==224 && CRT::Pixels()[(2*320+10)*4]==224,"Positive horizontal scroll");
        // FIFO writes capture both address and destination before later control writes.
        Genesis::SetRegionToPAL(false);YM7101::Init();Register(15,2);Register(1,0x40);
        Command(0x200,1);
        for(unsigned i=0;i<4;++i) YM7101::WriteDataPortWORD(word(0x1200+i));
        Need((YM7101::ReadControlPortWORD()&0x300)==0x100,"FIFO full status");
        Command(2,3); // Change destination while the VRAM writes are pending.
        YM7101::Advance(3420);
        Need((YM7101::ReadControlPortWORD()&0x300)==0x200,"FIFO empty status");
        Command(0x200,0);for(unsigned i=0;i<4;++i) Need(YM7101::ReadDataPortWORD()==0x1200+i,"FIFO captured address/code");
        // Real bus DMA defers memory writes and halts the CPU until transferred.
        Genesis::M68KWriteMemoryWORD(0xff0000,0x9876);Genesis::M68KWriteMemoryWORD(0xff0002,0x5432);
        Register(1,0x10);Register(19,2);Register(20,0);Register(21,0);Register(22,0x80);Register(23,0x7f);
        Command(0x400,0x21);Need(YM7101::TakeStallCycles()>0,"DMA must stall 68000");
        Need(YM7101::ReadControlPortWORD()&2,"DMA busy status");
        while(YM7101::TakeStallCycles()) YM7101::Advance(YM7101::TakeStallCycles());
        Need(!(YM7101::ReadControlPortWORD()&2),"DMA completion status");
        Command(0x400,0);Need(YM7101::ReadDataPortWORD()==0x9876 && YM7101::ReadDataPortWORD()==0x5432,"DMA RAM to VRAM transfer");
        // Fill starts only when the CPU writes its data seed after the command.
        Register(19,2);Register(20,0);Register(23,0x80);Command(0x600,0x21);
        Need(!YM7101::TakeStallCycles(),"VRAM fill must not halt CPU");
        YM7101::WriteDataPortWORD(0xabcd);YM7101::Advance(3420);
        Command(0x600,0);Need(YM7101::ReadDataPortWORD()==0xabab,"DMA fill seed/lane");
        Genesis::SetRegionToPAL(false);GamePad::Init();
        GamePad::Write(0,0x40,true);GamePad::Write(0,0x40,false);
        GamePad::SetButton(0,GamePad::MD_X,true);GamePad::SetButton(0,GamePad::MD_Z,true);
        for(unsigned i=0;i<2;++i) {GamePad::Write(0,0,false);GamePad::Write(0,0x40,false);}
        GamePad::Write(0,0,false);Need((GamePad::ReadData(0)&15)==0,"Sixbutton identification");
        GamePad::Write(0,0x40,false);Need((GamePad::ReadData(0)&15)==10,"Sixbutton XYZ mapping");
        GamePad::Advance(100000);Need((GamePad::ReadData(0)&15)==15,"Controller handshake timeout");
        std::vector<byte> rom(0x4000,0);rom[0x1b0]='R';rom[0x1b1]='A';BE32(rom,0x1b4,0x200001);BE32(rom,0x1b8,0x20000f);
        const std::string path=std::string(argv[1])+"/save-test.bin";
        std::remove((path+".srm").c_str());Cartridge::Load(rom,path);Need(Cartridge::HasSaveRAM(),"Missing SRAM header");
        Cartridge::Write(0x200001,0x42);Cartridge::Save();Cartridge::Load(rom,path);Need(Cartridge::Read(0x200001)==0x42,"SRAM persistence");
        Cartridge::Write(0xa130f1,3);Cartridge::Write(0x200001,0x99);Need(Cartridge::Read(0x200001)==0x42,"SRAM write protection");
        std::vector<byte> big(0x600000);for(unsigned bank=0;bank<12;++bank) std::fill(big.begin()+bank*0x80000,big.begin()+(bank+1)*0x80000,byte(bank));
        Cartridge::Load(big,path);Cartridge::Write(0xa130f3,9);Need(Cartridge::Read(0x80000)==9,"Cartridge bank switching");
        std::cout<<"Console memory, VDP clocking, framebuffer, six-button pads, save RAM and banking passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
