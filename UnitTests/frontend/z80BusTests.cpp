#include "../../Genesis/genesis.hpp"
#include "../../Audio/audio.hpp"
#include "../../Z80/z80.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
void Need(bool condition,const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
void Reg(unsigned index,unsigned value)
{
    YM7101::WriteControlPortWORD(word(0x8000 | (index << 8) | value));
}
void Command(unsigned address,unsigned code)
{
    YM7101::WriteControlPortWORD(word((address & 0x3fff) | ((code & 3) << 14)));
    YM7101::WriteControlPortWORD(word(((address >> 14) & 3) | ((code & 0x3c) << 2)));
}
void Setup()
{
    Genesis::SetRegionToPAL(false);
    Genesis::Init();YM7101::Init();Reg(15,2);
}
void Store(std::vector<byte>& code,word address,byte value)
{
    code.insert(code.end(),{0x3e,value,0x32,byte(address),byte(address >> 8)});
}
void Load(std::vector<byte>& code,word address,word destination)
{
    code.insert(code.end(),{0x3a,byte(address),byte(address >> 8),
                           0x32,byte(destination),byte(destination >> 8)});
}
void Execute(std::vector<byte> program,unsigned slice=15*4000)
{
    program.push_back(0x76); // HALT
    Z80::SetReset(true);Z80::SetBusRequest(true);
    for (unsigned i=0;i<program.size();++i) Z80::Write68k(i,program[i]);
    Z80::SetReset(false);Z80::SetBusRequest(false);
    unsigned remaining=15*4000;
    while(remaining)
    {
        const unsigned elapsed=std::min(remaining,slice);
        Z80::Advance(elapsed);remaining-=elapsed;
    }
    Z80::SetBusRequest(true);Z80::Advance(15 * 100);
    Need(Z80::BusGranted(),"Z80 failed to return its bus after the program");
}
void BusWaitClocks()
{
    std::vector<std::int16_t> expected;
    std::uint64_t expectedTicks=0,expectedWait=0;
    for(unsigned slice : {60000u,17u})
    {
        Setup();Reg(1,0x40);Command(0x300,1);
        for(unsigned i=0;i<4;++i) YM7101::WriteDataPortWORD(word(0x1230+i));
        Command(0x300,0);
        std::vector<byte> program;
        Store(program,0x7f11,0x84);Store(program,0x7f11,0x0d);Store(program,0x7f11,0x90);
        Load(program,0x7f00,0x200);Load(program,0x7f01,0x201);
        Store(program,0x202,0x99); // Execution must resume after the blocking read.
        Execute(program,slice);
        Need(Z80::Read68k(0x200)==0x12 && Z80::Read68k(0x201)==0x31 && Z80::Read68k(0x202)==0x99,
             "A VDP FIFO wait recursively advanced or corrupted the pending Z80 read");
        const auto wait=Genesis::MasterCycles();
        Need(wait>0,"Queued VDP FIFO read did not exercise a nested console stall");
        const auto audio=Audio::DrainSamples();
        Need(audio.size()==(std::uint64_t(61500+wait)*48000/53693175)*2,
             "Audio lost or duplicated master clocks during Z80 VDP waits");
        if(expected.empty())
        {
            expected=audio;expectedTicks=Z80::ExecutedCycles();expectedWait=wait;
        }
        else
        {
            Need(audio==expected,"VDP-wait audio changed with host scheduling slices");
            Need(Z80::ExecutedCycles()==expectedTicks && wait==expectedWait,
                 "VDP-wait clock accounting changed with host scheduling slices");
        }
    }
}
void VdpWriteBackPressure()
{
    Setup();Reg(1,0x40);Command(0x400,1);
    std::vector<byte> program;
    // More writes than the four-word FIFO can hold, without discarding data.
    for(unsigned i=1;i<=9;++i) Store(program,0x7f00,byte(i*0x11));
    Store(program,0x202,0x99);
    Execute(program);
    Need(Genesis::MasterCycles()>0,"VDP writes did not exercise FIFO back pressure");
    Need(Z80::Read68k(0x202)==0x99,"Z80 failed to continue after a VDP FIFO write wait");
    Command(0x400,0);
    for(unsigned i=1;i<=9;++i)
        Need(YM7101::ReadDataPortWORD()==i*0x1111,"A VDP FIFO write wait lost or duplicated a Z80 write");
}
void VdpBridge()
{
    Setup(); // Start in H32, then write register 12 from the Z80.
    std::vector<byte> program;
    Store(program,0x7f06,0x8c); // Duplicated $8C8C writes VDP register 12.
    Store(program,0x7f04,0x40); // Duplicated $4040: VRAM write at $0040.
    Store(program,0x7f07,0);    // The second command word; also exercises a mirror.
    Store(program,0x7f03,0x5a);
    Execute(program);
    Need(!YM7101::GetIs256Screen(),"Z80 VDP control-port mirror was not connected");
    Command(0x40,0);
    Need(YM7101::ReadDataPortWORD()==0x5a5a,"Z80 VDP byte write did not duplicate both word lanes");

    Setup();Command(0x100,1);
    YM7101::WriteDataPortWORD(0xabcd);YM7101::WriteDataPortWORD(0x1234);
    YM7101::WriteDataPortWORD(0x5678);YM7101::WriteDataPortWORD(0x9abc);
    Command(0x100,0);program.clear();
    for(unsigned i=0;i<4;++i) Load(program,word(0x7f00+i),word(0x200+i));
    Execute(program);
    const byte expected[]={0xab,0x34,0x56,0xbc};
    for(unsigned i=0;i<4;++i)
        Need(Z80::Read68k(0x200+i)==expected[i],"Z80 VDP data read lane or address increment was incorrect");

    Setup();YM7101::Advance(3420*7+101);Reg(0,2); // Freeze HV for independent lane checks.
    const word hv=YM7101::GetHVCounter();
    const word status=YM7101::ReadControlPortWORD();program.clear();
    Load(program,0x7f04,0x200);Load(program,0x7f07,0x201);
    for(unsigned i=0;i<8;++i) Load(program,word(0x7f08+i),word(0x202+i));
    Load(program,0x7f18,0x20a);Load(program,0x7f24,0x20b);
    Execute(program);
    Need(Z80::Read68k(0x200)==byte((status>>8)|0xfc),"Z80 VDP status upper floating bits must read high");
    Need(Z80::Read68k(0x201)==byte(status),"Z80 VDP status lower mirror was not connected");
    for(unsigned i=0;i<8;++i)
        Need(Z80::Read68k(0x202+i)==byte(i&1 ? hv:hv>>8),"Z80 HV counter byte lane/mirror mismatch");
    Need(Z80::Read68k(0x20a)==0xff && Z80::Read68k(0x20b)==0xff,"Unused Z80 VDP addresses must float high");
}
void PsgAliases()
{
    std::vector<std::int16_t> expected;
    for(unsigned alias=0x7f11;alias<=0x7f17;alias+=2)
    {
        Setup();std::vector<byte> program;
        Store(program,word(alias),0x84);Store(program,word(alias),0x0d);Store(program,word(alias),0x90);
        Execute(program);Z80::Advance(53693175/20);
        const auto samples=Audio::DrainSamples();
        Need(std::any_of(samples.begin(),samples.end(),[](std::int16_t value){return value>1000;}),
             "Z80 PSG alias produced no tone");
        if(expected.empty()) expected=samples;
        else Need(samples==expected,"Z80 PSG aliases have different sound behavior");
    }
}
}
int main()
{
    try
    {
        VdpBridge();BusWaitClocks();VdpWriteBackPressure();PsgAliases();
        std::cout<<"Z80 VDP data/control/status/HV byte lanes and all PSG aliases passed\n";
        return 0;
    }
    catch(const std::exception& error)
    {
        std::cerr<<error.what()<<'\n';return 1;
    }
}
