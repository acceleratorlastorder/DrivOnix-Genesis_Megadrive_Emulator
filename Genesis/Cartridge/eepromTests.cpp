#include "eeprom.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
void Require(bool condition,const char* message) { if(!condition)throw std::runtime_error(message); }
void Lines(bool clock,bool data) { EEPROM::WriteLines(clock,data); }
void Start() { Lines(true,true);Lines(true,false);Lines(false,false); }
void Stop() { Lines(false,false);Lines(true,false);Lines(true,true); }
bool Send(byte value)
{
    for(int bit=7;bit>=0;--bit)
    {
        const bool data=((value>>bit)&1)!=0;
        Lines(false,data);Lines(true,data);Lines(false,data);
    }
    Lines(false,true);Lines(true,true);
    const bool acknowledged=!EEPROM::ReadSDA();
    Lines(false,true);
    return acknowledged;
}
byte Receive(bool acknowledge)
{
    byte value=0;
    for(unsigned bit=0;bit<8;++bit)
    {
        Lines(false,true);Lines(true,true);
        value=byte((value<<1)|(EEPROM::ReadSDA()?1:0));
        Lines(false,true);
    }
    Lines(false,!acknowledge);Lines(true,!acknowledge);Lines(false,!acknowledge);
    return value;
}
byte Control(unsigned address,bool read,unsigned size)
{
    return byte(0xa0|((size<=2048?address>>8:0)<<1)|(read?1:0));
}
void Address(unsigned address,unsigned size)
{
    Require(Send(Control(address,false,size)),"EEPROM device did not ACK its address");
    if(size>2048) Require(Send(byte(address>>8)),"EEPROM high address byte was not ACKed");
    Require(Send(byte(address)),"EEPROM low address byte was not ACKed");
}
void Standard(EEPROM::Profile profile,unsigned size,unsigned page)
{
    EEPROM::Init(profile);
    Require(EEPROM::Bytes().size()==size,"Incorrect EEPROM capacity");
    Require(!EEPROM::Dirty(),"New EEPROM incorrectly dirty");
    const unsigned address=size>2048?0x123e:size/2+0x3e;
    Start();Address(address,size);
    Require(Send(0x5a)&&Send(0xa5)&&Send(0x96),"EEPROM data write did not ACK");
    Require(!EEPROM::Dirty(),"EEPROM committed data before STOP");
    Stop();
    Require(EEPROM::Dirty(),"EEPROM write was not marked dirty");
    Start();Require(!Send(Control(address,false,size)),"EEPROM ACKed during its write cycle");Stop();
    EEPROM::Advance(268465);
    Start();Require(!Send(Control(address,false,size)),"EEPROM write cycle ended early");Stop();
    EEPROM::Advance(1);
    Require(EEPROM::Bytes()[address]==0x5a,"EEPROM wrote at the wrong address");
    Require(EEPROM::Bytes()[(address&~(page-1))|((address+1)&(page-1))]==0xa5,"EEPROM second page byte incorrect");
    Require(EEPROM::Bytes()[(address&~(page-1))|((address+2)&(page-1))]==0x96,"EEPROM page wrap failed");
    Start();Address(address,size);Start();
    Require(Send(Control(address,true,size)),"EEPROM random read control did not ACK");
    Require(Receive(true)==0x5a,"EEPROM random read returned the wrong value");
    Require(Receive(false)==0xa5,"EEPROM sequential read returned the wrong value");
    Stop();
    EEPROM::ClearDirty();
    Require(!EEPROM::Dirty(),"EEPROM dirty clear failed");
    const auto saved=EEPROM::Bytes();
    EEPROM::Init(profile,saved);
    Require(EEPROM::Bytes()[address]==0x5a,"EEPROM persisted data did not reload");
    Start();Require(!Send(0xb0),"EEPROM incorrectly ACKed an unrelated I2C device");Stop();
}
std::vector<byte> Image(const char* product)
{
    std::vector<byte> rom(0x400,0);
    const std::string id(product);
    std::copy(id.begin(),id.end(),rom.begin()+0x180);
    return rom;
}
}
int main()
{
    try
    {
        using P=EEPROM::Profile;
        EEPROM::Init(P::SegaX24C01);
        Start();Require(Send(0x24<<1),"X24C01 write address was not ACKed");
        Require(Send(0x69),"X24C01 write data was not ACKed");Stop();
        EEPROM::Advance(268466);
        Start();Require(Send((0x24<<1)|1),"X24C01 read address was not ACKed");
        Require(Receive(false)==0x69,"X24C01 7-bit protocol read failed");Stop();
        Standard(P::AcclaimX24C02,256,4);
        Standard(P::Acclaim24C02,256,8);
        Standard(P::Acclaim24C04,512,16);
        Standard(P::Codemasters24C08,1024,16);
        Standard(P::Acclaim24C16,2048,16);
        Standard(P::Acclaim24C65,8192,64);
        Require(EEPROM::Detect(Image("GM T-12046-00"))==P::SegaX24C01,"Sega profile detection failed");
        Require(EEPROM::Detect(Image("GM T-50176-00"))==P::EaX24C01,"EA profile detection failed");
        Require(EEPROM::Detect(Image("GM T-081326-00"))==P::AcclaimX24C02,"Acclaim profile detection failed");
        auto sram=Image("GM T-12046-00");
        sram[0x1b0]='R';sram[0x1b1]='A';sram[0x1b4]=0;sram[0x1b5]=0x20;
        sram[0x1b8]=0;sram[0x1b9]=0x20;sram[0x1bb]=0xff;
        Require(EEPROM::Detect(sram)==P::None,"SRAM-patched cartridge was incorrectly detected as EEPROM");
        auto unknown=Image("GM UNKNOWN-00");
        unknown[0x1b0]='R';unknown[0x1b1]='A';
        Require(EEPROM::Detect(unknown)==P::None,"Ordinary SRAM header was detected as EEPROM");
        unknown[0x1b2]=0xe8;
        Require(EEPROM::Detect(unknown)==P::SegaX24C01,"Explicit serial EEPROM header was ignored");
        EEPROM::Init(P::EaX24C01);
        EEPROM::Write16(0x200000,0xc0);
        Require((EEPROM::Read8(0x200001)&0x80)!=0,"EA SDA output mapping failed");
        EEPROM::Write8(0x200001,0);
        Require((EEPROM::Read8(0x200001)&0x80)==0,"EA SDA input mapping failed");
        EEPROM::Init(P::Acclaim24C04);
        EEPROM::Write16(0x200000,1);
        Require(!EEPROM::IsMappedRead(0x200001),"Acclaim PAL did not select ROM reads");
        EEPROM::Write16(0x200000,0);
        Require(EEPROM::IsMappedRead(0x200001),"Acclaim PAL did not select EEPROM reads");
        bool rejected=false;
        try { EEPROM::Init(P::SegaX24C01,std::vector<byte>(7)); }
        catch(const std::exception&) { rejected=true; }
        Require(rejected,"Malformed EEPROM save was accepted");
        std::cout<<"EEPROM address protocols, ACK/NACK, repeated start, page writes, persistence and board mappings passed\n";
        return 0;
    }
    catch(const std::exception& error)
    {
        std::cerr<<error.what()<<'\n';return 1;
    }
}
