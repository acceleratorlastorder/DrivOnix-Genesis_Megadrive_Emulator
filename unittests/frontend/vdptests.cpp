#include "../../genesis/genesis.hpp"
#include "../../cathoderaytube/crt.hpp"
#include "../../ym7101/ym7101.hpp"
#include <array>
#include <iostream>
#include <stdexcept>

namespace
{
void Need(bool condition,const char* message) { if(!condition)throw std::runtime_error(message); }
void Reg(unsigned reg,unsigned value) { YM7101::WriteControlPortWORD(word(0x8000|(reg<<8)|value)); }
void Cmd(unsigned address,unsigned code)
{
    YM7101::WriteControlPortWORD(word((address&0x3fff)|((code&3)<<14)));
    YM7101::WriteControlPortWORD(word(((address>>14)&3)|((code&0x3c)<<2)));
}
void Vram(unsigned address,word value) { Cmd(address,1);YM7101::WriteDataPortWORD(value); }
void Cram(unsigned index,word value) { Cmd(index*2,3);YM7101::WriteDataPortWORD(value); }
void Tile(unsigned tile,unsigned color,unsigned rows=8)
{
    Cmd(tile*rows*4,1);
    for(unsigned i=0;i<rows*2;++i) YM7101::WriteDataPortWORD(word(color*0x1111));
}
void Setup()
{
    Genesis::SetRegionToPAL(false);YM7101::Init();
    Reg(15,2);Reg(0,4);Reg(2,0x30);Reg(3,0x2c);Reg(4,7);Reg(5,0x6c);Reg(12,0x81);Reg(13,0x3f);Reg(16,1);
    Cram(1,0x000e);Cram(2,0x00e0);Cram(3,0x0e00);Cram(4,0x00ee);
    Cram(5,0x0ee0);Cram(6,0x0eee);Cram(7,0x0e0e);Cram(8,0x000a);
}
void Frames() { YM7101::Advance(3420*262*2); }
std::array<unsigned,3> Pixel(unsigned x,unsigned y=2)
{
    const byte* p=CRT::Pixels()+(y*320+x)*4;
    return {{p[0],p[1],p[2]}};
}
void Color(unsigned x,unsigned y,std::array<unsigned,3> expected,const char* message)
{
    if(Pixel(x,y)!=expected)
    {
        const auto p=Pixel(x,y);
        std::cerr<<message<<" RGB="<<p[0]<<','<<p[1]<<','<<p[2]<<'\n';
        throw std::runtime_error(message);
    }
}
void Sprite(unsigned index,unsigned link,word attribute,unsigned x=128,unsigned y=128,unsigned dimensions=0)
{
    Cmd(0xd800+index*8,1);
    YM7101::WriteDataPortWORD(word(y));YM7101::WriteDataPortWORD(word((dimensions<<8)|link));
    YM7101::WriteDataPortWORD(attribute);YM7101::WriteDataPortWORD(word(x));
}
void PriorityTests()
{
    Setup();Tile(1,1);Tile(2,2);Tile(3,3);Tile(4,4);
    Vram(0xc000,1);Vram(0xe000,2);Sprite(0,0,3);Reg(1,0x44);Frames();
    Color(2,2,{{0,0,224}},"Low sprite must cover low A and B planes");
    Vram(0xe000,0x8002);Frames();
    Color(2,2,{{0,224,0}},"High B must cover low A and low sprite");
    Vram(0xc000,0x8001);Frames();
    Color(2,2,{{224,0,0}},"High A must cover equally high B");
    Sprite(0,0,0x8003);Frames();
    Color(2,2,{{0,0,224}},"High sprite must cover high A and B");
    Sprite(0,1,3);Sprite(1,0,0x8004);Frames();
    Color(2,2,{{224,0,0}},"Earlier low sprite must mask later high sprite even when hidden by a plane");
    while(YM7101::Scanline()!=0) YM7101::Advance(3420-YM7101::LineCycles());
    YM7101::ReadControlPortWORD();YM7101::Advance(3420-YM7101::LineCycles());
    Need(YM7101::ReadControlPortWORD()&0x20,"Overlapping opaque sprites must set collision");
    Need(!(YM7101::ReadControlPortWORD()&0x20),"Reading status must clear sprite collision");
}
void WindowTests()
{
    Setup();Tile(1,1);Tile(2,2);Vram(0xc000,1);Vram(0xe000,2);
    Reg(17,0x80); // Right window beginning at zero occupies the complete line.
    Reg(1,0x44);Frames();
    Color(2,2,{{0,224,0}},"Transparent window must suppress plane A and reveal B");
    Cmd(3*32,1);
    for(unsigned row=0;row<8;++row) { YM7101::WriteDataPortWORD(0x1234);YM7101::WriteDataPortWORD(0x5678); }
    Vram(0xb000,3);Frames();
    Color(4,2,{{0,224,224}},"Window must fetch the fifth pixel from byte two");
    Color(6,2,{{224,0,224}},"Window must fetch the seventh pixel from byte three");
    Vram(0xb000,0x0803);Frames();
    Color(0,2,{{160,0,0}},"Window horizontal flip must reverse all eight pixels");
    // Register 3 bit 1 is ignored by the H40 window table base decoder.
    Reg(3,0x2e);Frames();
    Color(0,2,{{160,0,0}},"H40 window table must be aligned to 4 KiB");
}
void ScrollAndSpriteTests()
{
    Setup();Tile(1,1);Tile(2,2);Tile(3,3);Tile(4,4);
    Vram(0xc000,1);Vram(0xc004,1);Vram(0xc080,3);Vram(0xc084,3);
    Reg(11,4);Cmd(0,5);YM7101::WriteDataPortWORD(0);YM7101::WriteDataPortWORD(0);
    YM7101::WriteDataPortWORD(8);YM7101::WriteDataPortWORD(0);Reg(1,0x44);Frames();
    Color(2,2,{{224,0,0}},"Two-cell vertical scroll first column");
    Color(18,2,{{0,0,224}},"Two-cell vertical scroll second column");
    Setup();Tile(1,1);Tile(2,2);Tile(3,3);Tile(4,4);
    Sprite(0,0,1,128,128,5);Reg(1,0x44);Frames();
    Color(2,2,{{224,0,0}},"Multi-tile sprite top-left tile");
    Color(2,10,{{0,224,0}},"Multi-tile sprite column-major bottom-left tile");
    Color(10,2,{{0,0,224}},"Multi-tile sprite column-major top-right tile");
    Sprite(0,0,0x0801,128,128,5);Frames();
    Color(2,2,{{0,0,224}},"Sprite horizontal flip must reverse columns");
    Sprite(0,0,0x1001,128,128,5);Frames();
    Color(2,2,{{0,224,0}},"Sprite vertical flip must reverse rows");
}
void ShadowHighlightTests()
{
    Setup();Tile(1,1);Tile(3,14);Vram(0xc000,1);
    Cram(0x3e,0x0eee);Cram(0x3f,0x0eee);
    Reg(12,0x89);Reg(1,0x44);Frames();
    Color(2,2,{{112,0,0}},"Low-priority planes must use shadow intensity");
    Sprite(0,0,0x6003);Frames();
    Color(2,2,{{224,0,0}},"Highlight operator must raise shadow to normal intensity");
    Vram(0xc000,0x8001);Sprite(0,0,0xe003);Frames();
    Color(2,2,{{240,128,128}},"Highlight intensity must use the four-bit VDP ladder");
    Tile(3,15);Frames();
    Color(2,2,{{112,0,0}},"Shadow operator must lower normal intensity");
    Reg(12,0x81);Tile(3,7);Sprite(0,0,0x8003);Frames();
    Color(2,2,{{224,0,224}},"Magenta is an opaque color, not a transparency key");
}
void InterlaceTests()
{
    Setup();Reg(12,0x87);Need(CRT::Height()==448,"Interlace2 must expose a 448-line framebuffer");
    Cmd(64,1);
    for(unsigned row=0;row<16;++row)
        for(unsigned half=0;half<2;++half) YM7101::WriteDataPortWORD(row&1?0x2222:0x1111);
    Vram(0xc000,1);Reg(1,0x44);Frames();
    Color(2,0,{{224,0,0}},"Interlace2 even field must fetch even pattern rows");
    Color(2,1,{{0,224,0}},"Interlace2 odd field must fetch odd pattern rows");
    Vram(0xc000,0x1001);Frames();
    Color(2,0,{{0,224,0}},"Interlace2 vertical flip must reverse sixteen pattern rows");
    Genesis::SetRegionToPAL(true);Reg(1,0x4c);Need(CRT::Height()==480,"PAL Interlace2 must expose 480 lines");
    YM7101::Advance(3420*313*2);
    Need(YM7101::Scanline()<313,"PAL Interlace2 scanline bound");
}
void CounterTests()
{
    Genesis::SetRegionToPAL(false);YM7101::Init();
    Need(YM7101::GetHCounter()==0x85,"H32 counter must start at its HINT phase");
    YM7101::Advance(299);Need(YM7101::GetHCounter()==0x93,"H32 counter before discontinuity");
    YM7101::Advance(1);Need(YM7101::GetHCounter()==0xe9,"H32 counter discontinuity");
    YM7101::Advance(460);Need(YM7101::GetHCounter()==0,"H32 counter wraps to zero during blanking");
    YM7101::Init();Reg(12,0x81);Need(YM7101::GetHCounter()==0xa5,"H40 counter origin");
    YM7101::Advance(279);Need(YM7101::GetHCounter()==0xb6,"H40 before jump");
    YM7101::Advance(1);Need(YM7101::GetHCounter()==0xe4,"H40 jump");
    YM7101::Advance(41);Need(YM7101::GetHCounter()==0xe7,"H40 variable sync clock");
    YM7101::Advance(459);Need(YM7101::GetHCounter()==0,"H40 zero wrap");
    YM7101::Init();Reg(1,0x40);
    YM7101::Advance(279);Need(!(YM7101::ReadControlPortWORD()&4),"H32 HBLANK asserted early");
    YM7101::Advance(1);Need(YM7101::ReadControlPortWORD()&4,"H32 HBLANK did not assert");
    YM7101::Advance(580);Need(!(YM7101::ReadControlPortWORD()&4),"H32 HBLANK did not clear");
    Reg(0,6);const word held=YM7101::GetHVCounter();
    YM7101::Advance(3420*3+113);
    Need(YM7101::GetHVCounter()==held,"HV counter freeze did not preserve both counters");
    Reg(0,4);Need(YM7101::GetHVCounter()!=held,"Clearing HV counter freeze did not resume counters");
}
void InterruptTests()
{
    Setup();Reg(1,0x64);Reg(0,0x14);Reg(10,0);
    YM7101::Advance(3420*262); // Let the HINT downcounter reload during blanking.
    YM7101::GetIntType();YM7101::Advance(3420);
    Need(YM7101::GetRequestInt(),"HINT source did not assert");
    YM7101::ReadControlPortWORD();
    Need(YM7101::GetRequestInt(),"A status read must not acknowledge the CPU's interrupt");
    Need(YM7101::GetIntType()==4,"HINT must request interrupt level 4");
    while(YM7101::Scanline()!=224) YM7101::Advance(3420-YM7101::LineCycles());
    YM7101::Advance(788-YM7101::LineCycles());
    Need(YM7101::GetIntType()==6,"VINT must take priority over pending HINT");
    Need(YM7101::GetRequestInt()&&YM7101::GetIntType()==4,"Acknowledging VINT must leave pending HINT asserted");
}
void CpuInterruptIntegration()
{
    std::vector<byte> rom(0x400,0);
    auto wordAt=[&](unsigned at,word value) {rom[at]=byte(value>>8);rom[at+1]=byte(value);};
    auto longAt=[&](unsigned at,dword value) {wordAt(at,word(value>>16));wordAt(at+2,word(value));};
    longAt(0,0xff1000);longAt(4,0x200);longAt(0x78,0x300);
    wordAt(0x200,0x46fc);wordAt(0x202,0x2000); // MOVE #$2000,SR
    wordAt(0x204,0x4e72);wordAt(0x206,0x2000); // STOP #$2000
    wordAt(0x208,0x60fa); // BRA back to STOP
    const word handler[]={0x48e7,0xfffe,0x5239,0x00ff,0x0000,0x4cdf,0x7fff,0x4e73};
    for(unsigned i=0;i<8;++i) wordAt(0x300+i*2,handler[i]);
    Genesis::SetRegionToPAL(false);Genesis::ResetM68kMemory();Genesis::InsertCartridge(rom,"vdp-irq-generated.bin");
    Genesis::Init();M68k::Init();YM7101::Init();Reg(1,0x64);
    for(unsigned frame=0;frame<10;++frame) Genesis::RunFrame();
    Need(Genesis::M68KReadMemoryBYTE(0xff0000)==10,"Generated VINT handler must execute once per frame");
    const auto cpu=M68k::GetCpuState();
    Need(cpu.registerAddress[7]==0xff1000,"IRQ/MOVEM/RTE sequence did not restore stack pointer");
    Need(cpu.programCounter>=0x200&&cpu.programCounter<=0x208,"IRQ/MOVEM/RTE sequence corrupted program counter");
}

}
int main()
{
    try
    {
        Genesis::AllocM68kMemory();Genesis::ResetM68kMemory();Genesis::Init();
        PriorityTests();WindowTests();ScrollAndSpriteTests();ShadowHighlightTests();InterlaceTests();CounterTests();InterruptTests();CpuInterruptIntegration();
        std::cout<<"VDP plane/sprite priorities, collisions, window fetch/flip, scroll, interlace fields and IRQ latches passed\n";
        return 0;
    }
    catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
