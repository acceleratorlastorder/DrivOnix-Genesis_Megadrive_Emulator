#include "eeprom.hpp"
#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{
using Profile=EEPROM::Profile;
enum class Mode { Idle, Control, Address, Receive, Send, MasterAck };
struct Device
{
    Profile profile=Profile::None;
    std::uint32_t masterClock=53693175;
    std::uint64_t busyClocks=0;
    std::vector<byte> bytes;
    std::vector<std::pair<unsigned,byte>> pending;
    Mode mode=Mode::Idle, afterAck=Mode::Idle;
    unsigned pageSize=0, addressBytes=1, addressRemaining=0;
    unsigned pointer=0, bank=0, bits=0, received=0, ackStage=0, addressValue=0;
    bool masterSda=true, scl=true, driveLow=false, ack=false, dirty=false, romVisible=false;
};
Device device;

bool Acclaim32()
{
    return device.profile==Profile::Acclaim24C02||device.profile==Profile::Acclaim24C04||
           device.profile==Profile::Acclaim24C16||device.profile==Profile::Acclaim24C65;
}
bool Codemasters()
{
    return device.profile==Profile::Codemasters24C08||device.profile==Profile::Codemasters24C16||
           device.profile==Profile::Codemasters24C65;
}
bool SevenBitAddress()
{
    return device.profile==Profile::SegaX24C01||device.profile==Profile::EaX24C01;
}
void Begin()
{
    device.mode=Mode::Control;
    device.bits=device.received=device.ackStage=0;
    device.driveLow=false;
}
void Stop()
{
    const bool wrote=!device.pending.empty();
    for(const auto& item:device.pending)
        if(device.bytes[item.first]!=item.second)
        {
            device.bytes[item.first]=item.second;
            device.dirty=true;
        }
    if(wrote) device.busyClocks=(device.masterClock+199)/200; // maximum 5 ms write cycle
    device.pending.clear();
    device.mode=Mode::Idle;
    device.ackStage=0;
    device.driveLow=false;
}
void Received(byte value)
{
    device.ack=true;
    if(device.mode==Mode::Control&&device.busyClocks)
    {
        device.ack=false;
        device.afterAck=Mode::Idle;
    }
    else if(device.mode==Mode::Control)
    {
        if(SevenBitAddress())
        {
            device.pointer=(value>>1)&127;
            device.afterAck=(value&1)?Mode::Send:Mode::Receive;
        }
        else
        {
            const unsigned bankMask=device.addressBytes==1 ? (device.bytes.size()-1)>>8 : 0;
            const unsigned selection=(value>>1)&7;
            if((value&0xf0)!=0xa0 || (selection&~bankMask))
            {
                device.ack=false;
                device.afterAck=Mode::Idle;
            }
            else
            {
                device.bank=(selection&bankMask)<<8;
                if(device.addressBytes==1) device.pointer=(device.pointer&255)|device.bank;
                device.addressRemaining=device.addressBytes;
                device.addressValue=0;
                device.received=0;
                device.afterAck=(value&1)?Mode::Send:Mode::Address;
            }
        }
    }
    else if(device.mode==Mode::Address)
    {
        device.addressValue=(device.addressValue<<8)|value;
        if(--device.addressRemaining==0)
        {
            device.pointer=(device.bank|device.addressValue)&(device.bytes.size()-1);
            device.afterAck=Mode::Receive;
        }
        else device.afterAck=Mode::Address;
    }
    else
    {
        device.pending.push_back({device.pointer,value});
        const unsigned mask=device.pageSize-1;
        device.pointer=(device.pointer&~mask)|((device.pointer+1)&mask);
        device.afterAck=Mode::Receive;
    }
    device.ackStage=1;
    device.bits=0;
    // Address bytes use received as a multi-byte shift register; other modes
    // need a fresh byte accumulator when the ACK cycle finishes.
}
void Falling()
{
    if(device.ackStage==1)
    {
        device.driveLow=device.ack;
        device.ackStage=2;
        return;
    }
    if(device.ackStage==3)
    {
        device.ackStage=0;
        device.mode=device.afterAck;
        device.bits=0;
        device.received=0;
    }
    if(device.mode==Mode::Send)
        device.driveLow=((device.bytes[device.pointer]>>(7-device.bits))&1)==0;
    else device.driveLow=false;
}
void Rising()
{
    if(device.ackStage==2) { device.ackStage=3; return; }
    if(device.ackStage) return;
    if(device.mode==Mode::Send)
    {
        if(++device.bits==8)
        {
            device.mode=Mode::MasterAck;
            device.bits=0;
        }
    }
    else if(device.mode==Mode::MasterAck)
    {
        if(device.masterSda) device.mode=Mode::Idle;
        else
        {
            device.pointer=(device.pointer+1)&(device.bytes.size()-1);
            device.mode=Mode::Send;
            device.bits=0;
        }
    }
    else if(device.mode!=Mode::Idle)
    {
        // Keep the completed high address byte separately so normal byte shifts
        // and ACK clocks cannot discard a 16-bit EEPROM word address.
        device.received=(device.received<<1)|(device.masterSda?1:0);
        if(++device.bits==8) Received(static_cast<byte>(device.received));
    }
}
std::string Product(const std::vector<byte>& rom)
{
    return rom.size()>=0x200 ? std::string(rom.begin()+0x180,rom.begin()+0x18e) : std::string();
}
dword BE32(const std::vector<byte>& rom,unsigned at)
{
    return (dword(rom[at])<<24)|(dword(rom[at+1])<<16)|(dword(rom[at+2])<<8)|rom[at+3];
}
}

EEPROM::Profile EEPROM::Detect(const std::vector<byte>& rom)
{
    if(rom.size()<0x200) return Profile::None;
    const std::string id=Product(rom);
    auto has=[&](const char* code) { return id.find(code)!=std::string::npos; };
    // Board wiring/product identities are hardware facts. Protocol implementation
    // is original; see EEPROM.txt for primary datasheets and mapping references.
    if(has("T-50176")||has("T-50396")||has("T-50446")||has("T-50516")||has("T-50606")) return Profile::EaX24C01;
    const bool ramHeader=rom[0x1b0]=='R'&&rom[0x1b1]=='A';
    const bool patchedSram=ramHeader&&BE32(rom,0x1b8)>BE32(rom,0x1b4)+2;
    if((has("T-12046")||has("T-12053")||has("G-4060"))&&!patchedSram) return Profile::SegaX24C01;
    if(has("MK-1215")||has("MK-1228")||has("G-5538")||has("00001211")||has("00004076")||has("G-4524")) return Profile::SegaX24C01;
    if(has("T-81033")||has("T-081326")) return Profile::AcclaimX24C02;
    if(has("T-081276")) return Profile::Acclaim24C02;
    if(has("T-81406")) return Profile::Acclaim24C04;
    if(has("T-081586")) return Profile::Acclaim24C16;
    if(has("T-81476")||has("T-81576")) return Profile::Acclaim24C65;
    if(has("T-120106")) return Profile::Codemasters24C08;
    if(has("T-120096")) return Profile::Codemasters24C16;
    if(has("T-120146")) return Profile::Codemasters24C65;
    // Explicit serial-memory flag: never infer EEPROM from an ordinary RAM range.
    if(ramHeader&&rom[0x1b2]==0xe8) return Profile::SegaX24C01;
    return Profile::None;
}
void EEPROM::Init(Profile profile,const std::vector<byte>& persisted)
{
    device=Device{};
    device.profile=profile;
    unsigned size=0;
    switch(profile)
    {
    case Profile::None:return;
    case Profile::SegaX24C01:case Profile::EaX24C01:size=128;device.pageSize=4;break;
    case Profile::AcclaimX24C02:size=256;device.pageSize=4;break;
    case Profile::Acclaim24C02:size=256;device.pageSize=8;break;
    case Profile::Acclaim24C04:size=512;device.pageSize=16;break;
    case Profile::Codemasters24C08:size=1024;device.pageSize=16;break;
    case Profile::Acclaim24C16:case Profile::Codemasters24C16:size=2048;device.pageSize=16;break;
    case Profile::Acclaim24C65:case Profile::Codemasters24C65:size=8192;device.pageSize=64;device.addressBytes=2;break;
    }
    if(!persisted.empty()&&persisted.size()!=size) throw std::runtime_error("Incorrect EEPROM save size");
    device.bytes=persisted.empty()?std::vector<byte>(size,0xff):persisted;
}
void EEPROM::WriteLines(bool scl,bool sda)
{
    if(device.bytes.empty()) return;
    const bool oldClock=device.scl,oldData=device.masterSda;
    device.scl=scl;device.masterSda=sda;
    if(oldClock&&scl&&oldData!=sda)
    {
        if(sda) Stop(); else Begin();
        return;
    }
    if(oldClock&&!scl) Falling();
    else if(!oldClock&&scl) Rising();
}
bool EEPROM::ReadSDA() { return device.masterSda&&!device.driveLow; }
void EEPROM::SetMasterClock(std::uint32_t clock)
{
    if(clock<1000000||clock>100000000) throw std::invalid_argument("Invalid EEPROM master clock");
    device.masterClock=clock;
}
void EEPROM::Advance(std::uint64_t clocks)
{
    device.busyClocks=clocks>=device.busyClocks?0:device.busyClocks-clocks;
}
const std::vector<byte>& EEPROM::Bytes() { return device.bytes; }
bool EEPROM::Dirty() { return device.dirty; }
void EEPROM::ClearDirty() { device.dirty=false; }
bool EEPROM::IsMappedRead(dword address)
{
    if(device.bytes.empty()||!(address&1)) return false;
    if(Codemasters()) return address>=0x380000&&address<0x400000;
    if(Acclaim32()) return !device.romVisible&&address>=0x200000&&address<0x300000;
    return address>=0x200000&&address<0x400000;
}
bool EEPROM::IsMappedWrite(dword address)
{
    if(device.bytes.empty()) return false;
    if(Codemasters()) return address>=0x300000&&address<0x380000;
    if(Acclaim32()) return address>=0x200000&&address<0x300000;
    return address>=0x200000&&address<0x400000;
}
byte EEPROM::Read8(dword address,byte underlyingBus)
{
    if(!IsMappedRead(address)) return underlyingBus;
    const unsigned bit=device.profile==Profile::EaX24C01||Codemasters()?7:device.profile==Profile::AcclaimX24C02?1:0;
    return (underlyingBus&~(1u<<bit))|(ReadSDA()?1u<<bit:0);
}
void EEPROM::Write8(dword address,byte value)
{
    if(!IsMappedWrite(address)) return;
    if(Acclaim32())
    {
        if(address&1) WriteLines(device.scl,value&1); else WriteLines(value&1,device.masterSda);
    }
    else if(device.profile==Profile::AcclaimX24C02||Codemasters()) WriteLines(value&2,value&1);
    else if(address&1)
    {
        if(device.profile==Profile::EaX24C01) WriteLines(value&0x40,value&0x80);
        else WriteLines(value&2,value&1);
    }
}
void EEPROM::Write16(dword address,word value)
{
    if(!IsMappedWrite(address)&&!IsMappedWrite(address+1)) return;
    if(Acclaim32())
    {
        // On these PAL boards a word strobe selects ROM (D0=1) or EEPROM
        // (D0=0) reads; byte strobes address SCL and SDA independently.
        device.romVisible=(value&1)!=0;
    }
    else if(device.profile==Profile::EaX24C01) WriteLines(value&0x40,value&0x80);
    else WriteLines(value&2,value&1);
}
