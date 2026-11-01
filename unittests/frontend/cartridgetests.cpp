#include "../../genesis/genesis.hpp"
#include "../../genesis/cartridge/cartridge.hpp"
#include "../../genesis/cartridge/eeprom.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <direct.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

// Only this test executable wraps these calls. Production objects still call
// the real operating-system API, with no test hooks in the save implementation.
#ifdef DRIVONIX_IO_FAULT_TESTS
namespace SaveFault {
enum class Kind { None, Full, Rename };
Kind kind=Kind::None;
std::size_t remaining=0;
unsigned writes=0;
}
extern "C" ssize_t __real_write(int,const void*,std::size_t);
extern "C" int __real_rename(const char*,const char*);
extern "C" ssize_t __wrap_write(int fd,const void* bytes,std::size_t count)
{
    if(SaveFault::kind!=SaveFault::Kind::Full) return __real_write(fd,bytes,count);
    ++SaveFault::writes;
    if(!SaveFault::remaining) { errno=ENOSPC;return -1; }
    const std::size_t allowed=std::min(count,SaveFault::remaining);
    const auto written=__real_write(fd,bytes,allowed);
    if(written>0) SaveFault::remaining-=static_cast<std::size_t>(written);
    return written;
}
extern "C" int __wrap_rename(const char* from,const char* to)
{
    if(SaveFault::kind==SaveFault::Kind::Rename) { errno=EACCES;return -1; }
    return __real_rename(from,to);
}
#endif

namespace
{
void Need(bool pass,const char* why) { if(!pass) throw std::runtime_error(why); }
void Directory(const std::string& path)
{
#ifdef _WIN32
    const int result=_mkdir(path.c_str());
#else
    const int result=mkdir(path.c_str(),0700);
#endif
    Need(result==0||errno==EEXIST,"Cannot create cartridge fixture directory");
}
void RemoveDirectory(const std::string& path)
{
#ifdef _WIN32
    Need(_rmdir(path.c_str())==0,"Cannot remove cartridge fixture directory");
#else
    Need(rmdir(path.c_str())==0,"Cannot remove cartridge fixture directory");
#endif
}
std::vector<std::string> Names(const std::string& path)
{
    std::vector<std::string> result;
#ifdef _WIN32
    WIN32_FIND_DATAA item;
    HANDLE search=FindFirstFileA((path+"/*").c_str(),&item);
    if(search==INVALID_HANDLE_VALUE&&GetLastError()==ERROR_FILE_NOT_FOUND) return result;
    Need(search!=INVALID_HANDLE_VALUE,"Cannot inspect cartridge fixture directory");
    do { const std::string name=item.cFileName;if(name!="."&&name!="..") result.push_back(name); }
    while(FindNextFileA(search,&item));
    FindClose(search);
#else
    DIR* directory=opendir(path.c_str());
    Need(directory!=nullptr,"Cannot inspect cartridge fixture directory");
    while(const auto* item=readdir(directory))
    {
        const std::string name=item->d_name;
        if(name!="."&&name!="..") result.push_back(name);
    }
    closedir(directory);
#endif
    return result;
}
void RemoveTree(const std::string& path)
{
    for(const auto& name:Names(path))
    {
        const std::string child=path+"/"+name;
#ifdef _WIN32
        const bool directory=(GetFileAttributesA(child.c_str())&FILE_ATTRIBUTE_DIRECTORY)!=0;
#else
        struct stat info={};Need(lstat(child.c_str(),&info)==0,"Cannot stat fixture");
        const bool directory=S_ISDIR(info.st_mode);
#endif
        if(directory) RemoveTree(child);
        else Need(std::remove(child.c_str())==0,"Cannot remove cartridge fixture");
    }
    RemoveDirectory(path);
}
struct Sandbox
{
    std::string root;
    explicit Sandbox(const std::string& parent)
    {
        Directory(parent);
        root=parent+"/case-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        Directory(root);
    }
    ~Sandbox() { try { RemoveTree(root); } catch(...) {} }
    std::string Path(const std::string& name) const { return root+"/"+name; }
};
void WriteFile(const std::string& path,const std::vector<byte>& bytes)
{
    std::ofstream output(path,std::ios::binary|std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
    Need(output.good(),"Cannot write cartridge fixture");
}
std::vector<byte> ReadFile(const std::string& path)
{
    std::ifstream input(path,std::ios::binary|std::ios::ate);
    Need(bool(input),"Expected cartridge save file is missing");
    const auto length=input.tellg();Need(length>=0,"Cannot size cartridge fixture");
    std::vector<byte> bytes(static_cast<std::size_t>(length));input.seekg(0);
    if(!bytes.empty()) Need(bool(input.read(reinterpret_cast<char*>(bytes.data()),bytes.size())),"Cannot read cartridge fixture");
    return bytes;
}
void NoTemporary(const std::string& directory)
{
    for(const auto& name:Names(directory))
        Need(name.find(".tmp.")==std::string::npos,"Failed save left a partial temporary file");
}
bool SaveFails()
{
    try { Genesis::SaveCartridge();return false; }
    catch(const std::runtime_error&) { return true; }
}
void BE32(std::vector<byte>& rom,unsigned at,unsigned value)
{
    for(unsigned i=0;i<4;++i) rom[at+i]=byte(value>>(24-8*i));
}
std::vector<byte> RamImage(unsigned start,unsigned end,std::size_t size=0x800)
{
    std::vector<byte> rom(size,0x5a);
    rom[0x1b0]='R';rom[0x1b1]='A';rom[0x1b2]=0xf8;
    BE32(rom,0x1b4,start);BE32(rom,0x1b8,end);rom[0x1f0]='U';
    return rom;
}
void LoaderAndROM(const Sandbox& fixture)
{
    std::vector<byte> rom(0x601);
    for(unsigned i=0;i<rom.size();++i) rom[i]=byte(i*37+11);
    rom[0x1f0]='E';rom[0x18e]=rom[0x18f]=0;
    const auto path=fixture.Path("unaltered.BIN");WriteFile(path,rom);
    Genesis::SetRomName(path);RomLoader::LoadRomFile(path);
    Need(!RomLoader::ChecksumMatches(),"Checksum mismatch was hidden");
    Need(RomLoader::Bytes()==rom,"ROM loader rewrote the original header or payload");
    Genesis::InsertCartridge();
    for(unsigned i=0;i<rom.size();++i)
        Need(Genesis::M68KReadMemoryBYTE(i)==rom[i],"Cartridge bus did not preserve loader bytes");
    Genesis::M68KWriteMemoryLONG(0x300,0x12345678);
    Need(Genesis::M68KReadMemoryLONG(0x300)==(dword(rom[0x300])<<24|dword(rom[0x301])<<16|dword(rom[0x302])<<8|rom[0x303]),"ROM bus write modified cartridge image");
    Need(Genesis::M68KReadMemoryBYTE(rom.size())==0xff,"Unpopulated cartridge space did not float high");
    Need(ReadFile(path)==rom,"Loading modified the source ROM file");
}
void SRAM(const Sandbox& fixture)
{
    for(unsigned lane=0;lane<3;++lane)
    {
        const unsigned start=lane==1 ? 0x401:0x400;
        const unsigned end=lane==0 ? 0x40e:0x40f;
        const auto rom=RamImage(start,end);
        const auto path=fixture.Path("sram-"+std::to_string(lane)+".bin");
        Genesis::InsertCartridge(rom,path);
        Need(Cartridge::HasSaveRAM(),"SRAM header was ignored");
        Genesis::M68KWriteMemoryLONG(0x400,0x12345678);
        const dword expected=lane==0 ? 0x125a565a:lane==1 ? 0x5a345a78:0x12345678;
        Need(Genesis::M68KReadMemoryLONG(0x400)==expected,"SRAM byte lanes or word strobes are incorrect");
        Genesis::M68KWriteMemoryWORD(0xa130f0,3);
        Genesis::M68KWriteMemoryLONG(0x400,0xaabbccdd);
        Need(Genesis::M68KReadMemoryLONG(0x400)==expected,"SRAM write-protect bit was ignored");
        Genesis::M68KWriteMemoryBYTE(0xa130f1,0);
        Need(Genesis::M68KReadMemoryLONG(0x400)==0x5a5a5a5a,"Disabled SRAM did not expose ROM");
        Genesis::M68KWriteMemoryBYTE(start,0x99);
        Genesis::M68KWriteMemoryWORD(0xa130f0,1);
        Need(Genesis::M68KReadMemoryLONG(0x400)==expected,"Disabled SRAM accepted writes");
        Genesis::SaveCartridge();const auto saved=ReadFile(path+".srm");
        Need(saved.size()==(lane==2 ? 16u:8u),"SRAM file did not use physical byte-lane packing");
        Need(saved[0]==(lane==1 ? 0x34:0x12)&&saved[1]==(lane==2 ? 0x34:lane==1 ? 0x78:0x56),"SRAM persisted incorrect lane bytes");
        Genesis::InsertCartridge(rom,path);
        Need(Genesis::M68KReadMemoryLONG(0x400)==expected,"SRAM did not reload through the console bus");
    }
    const auto large=RamImage(0x200001,0x20000f,0x300000);
    Genesis::InsertCartridge(large,fixture.Path("overlap.bin"));
    Need(Genesis::M68KReadMemoryBYTE(0x200001)==0x5a,"Large-ROM SRAM overlay should start disabled");
    Genesis::M68KWriteMemoryBYTE(0xa130f1,1);
    Genesis::M68KWriteMemoryWORD(0x200000,0x89ab);
    Need(Genesis::M68KReadMemoryWORD(0x200000)==0x5aab,"Enabled SRAM overlay lost the unconnected ROM lane");
    Genesis::M68KWriteMemoryBYTE(0xa130f1,0);
    Need(Genesis::M68KReadMemoryWORD(0x200000)==0x5a5a,"Disabling SRAM failed to restore the overlapping ROM");
}
void Banking(const Sandbox& fixture)
{
    std::vector<byte> rom(0x800000);
    for(unsigned bank=0;bank<16;++bank)
        std::fill(rom.begin()+bank*0x80000,rom.begin()+(bank+1)*0x80000,byte(0x20+bank));
    Genesis::InsertCartridge(rom,fixture.Path("banks.bin"));
    for(unsigned window=0;window<8;++window)
        Need(Genesis::M68KReadMemoryBYTE(window*0x80000)==0x20+window,"Initial cartridge bank map is incorrect");
    for(unsigned window=1;window<8;++window)
    {
        const unsigned bank=8+window;
        Genesis::M68KWriteMemoryWORD(0xa130f0+window*2,word(0xff00|bank));
        Need(Genesis::M68KReadMemoryBYTE(window*0x80000)==0x20+bank,"Mapper word write selected the wrong bank/lane");
    }
    Genesis::M68KWriteMemoryBYTE(0xa130f2,0);
    Need(Genesis::M68KReadMemoryBYTE(0x80000)==0x29,"Unconnected even mapper lane changed a bank");
    Genesis::M68KWriteMemoryBYTE(0xa130f3,0x42);
    Need(Genesis::M68KReadMemoryBYTE(0x80000)==0x22,"Mapper did not mask six-bit bank selection");
    Genesis::M68KWriteMemoryBYTE(0xa130ff,0xff);
    Need(Genesis::M68KReadMemoryBYTE(0x380000)==0xff,"Unpopulated selected bank did not float high");
    Need(Genesis::M68KReadMemoryBYTE(0)==0x20,"Bank zero must remain fixed");
}

struct SerialBoard
{
    const char* product;
    unsigned size,address,readAddress,writeAddress,outputMask;
    enum Wiring { Sega,EA,Acclaim16,Acclaim32,Codemasters } wiring;
    bool words;
    void Lines(bool clock,bool data) const
    {
        if(wiring==Acclaim32)
        {
            Genesis::M68KWriteMemoryBYTE(writeAddress,clock?1:0);
            Genesis::M68KWriteMemoryBYTE(writeAddress+1,data?1:0);
        }
        else
        {
            const byte value=wiring==EA ? byte((clock?0x40:0)|(data?0x80:0)):byte((clock?2:0)|(data?1:0));
            if(words) Genesis::M68KWriteMemoryWORD(writeAddress,value);
            else Genesis::M68KWriteMemoryBYTE(writeAddress,value);
        }
    }
    bool Data() const { return (Genesis::M68KReadMemoryBYTE(readAddress)&outputMask)!=0; }
    void Start() const { Lines(true,true);Lines(true,false);Lines(false,false); }
    void Stop() const { Lines(false,false);Lines(true,false);Lines(true,true); }
    bool Send(byte value) const
    {
        for(int bit=7;bit>=0;--bit)
        {
            const bool data=(value&(1u<<bit))!=0;
            Lines(false,data);Lines(true,data);Lines(false,data);
        }
        Lines(false,true);Lines(true,true);const bool ack=!Data();Lines(false,true);return ack;
    }
    byte Receive() const
    {
        byte value=0;
        for(unsigned bit=0;bit<8;++bit)
        {
            Lines(false,true);Lines(true,true);value=byte((value<<1)|(Data()?1:0));Lines(false,true);
        }
        Lines(false,true);Lines(true,true);Lines(false,true);return value; // Master NACK.
    }
    byte Control(unsigned at,bool read) const
    {
        if(size==128) return byte((at<<1)|(read?1:0));
        return byte(0xa0|(size<=2048 ? (at>>8)<<1:0)|(read?1:0));
    }
    void Address(unsigned at) const
    {
        Need(Send(Control(at,false)),"Console EEPROM did not ACK write control");
        if(size!=128)
        {
            if(size>2048) Need(Send(byte(at>>8)),"Console EEPROM did not ACK high word address");
            Need(Send(byte(at)),"Console EEPROM did not ACK low word address");
        }
    }
    void Write(unsigned at,byte value) const
    {
        Start();Address(at);Need(Send(value),"Console EEPROM did not ACK data");Stop();
    }
    byte Read(unsigned at) const
    {
        Start();
        if(size!=128) { Address(at);Start(); }
        Need(Send(Control(at,true)),"Console EEPROM did not ACK read control");
        const byte value=Receive();Stop();return value;
    }
    bool Poll() const { Start();const bool ack=Send(Control(address,false));Stop();return ack; }
};
std::vector<byte> SerialImage(const SerialBoard& board)
{
    std::vector<byte> rom(0x400000,0x5a);
    const std::string product=board.product;
    std::copy(product.begin(),product.end(),rom.begin()+0x180);
    return rom;
}
void SerialPersistence(const Sandbox& fixture)
{
    const SerialBoard boards[]={
        {"GM T-12046-00",128,0x25,0x200001,0x200000,1,SerialBoard::Sega,true},
        {"GM T-50176-00",128,0x37,0x200001,0x200001,0x80,SerialBoard::EA,false},
        {"GM T-081326-00",256,0xb7,0x200001,0x200000,2,SerialBoard::Acclaim16,true},
        {"GM T-81476-00",8192,0x123e,0x200001,0x200000,1,SerialBoard::Acclaim32,false},
        {"GM T-120106-00",1024,0x241,0x380001,0x300000,0x80,SerialBoard::Codemasters,false}
    };
    for(unsigned index=0;index<sizeof(boards)/sizeof(boards[0]);++index)
    {
        const auto& board=boards[index];auto rom=SerialImage(board);
        rom[0x1f0]=index%2==0 ? 'E':'U';
        const auto path=fixture.Path("serial-"+std::to_string(index)+".bin");
        Genesis::SetRegionToPAL(index%2==0);Genesis::Init();YM7101::Init();
        if(index==0)
        {
            WriteFile(path,rom);Genesis::SetRomName(path);RomLoader::LoadRomFile(path);
            Genesis::InsertCartridge();
        }
        else Genesis::InsertCartridge(rom,path);
        // Both insertion overloads, including insertion after Init, must retain
        // the regional clock after the EEPROM device has been reinitialized.
        Need(!Cartridge::HasSaveRAM()&&EEPROM::Bytes().size()==board.size,"Console selected the wrong serial board/capacity");
        Need(Genesis::M68KReadMemoryBYTE(board.readAddress-1)==0x5a,"EEPROM read altered the unconnected ROM byte lane");
        board.Lines(true,true);
        Need((Genesis::M68KReadMemoryBYTE(board.readAddress)&~board.outputMask)==(0x5a&~board.outputMask),"EEPROM SDA replaced unrelated ROM data bits");
        board.Write(board.address,0x96);
        Need(EEPROM::Dirty(),"Bus EEPROM write was not marked dirty");
        Need(!board.Poll(),"Console EEPROM ACKed during its write cycle");
        const unsigned busy=(Genesis::MasterClock()+199)/200;
        Genesis::StallMasterCycles(busy-1);
        Need(!board.Poll(),"Console EEPROM busy interval ended before the regional 5 ms boundary");
        Genesis::StallMasterCycles(1);
        Need(board.Poll(),"Console EEPROM busy interval did not end at the regional 5 ms boundary");
        Need(board.Read(board.address)==0x96,"Console serial bus read the wrong value/address");
        Genesis::SaveCartridge();const auto saved=ReadFile(path+".eep");
        Need(saved.size()==board.size&&saved[board.address]==0x96,"Console EEPROM file has incorrect geometry/data");
        Need(!EEPROM::Dirty(),"Successful EEPROM save did not clear dirty state");
        Genesis::InsertCartridge(rom,path);
        Need(board.Read(board.address)==0x96,"Same-file EEPROM reload lost persisted bytes");
        board.Write(board.address+1,0x69);
        Genesis::InsertCartridge(rom,path); // Must save dirty bytes before reinitializing the device.
        Need(board.Read(board.address+1)==0x69,"Same-file dirty EEPROM reload discarded the latest write");
        board.Write(board.address+2,0x3c);
        Genesis::StallMasterCycles(busy-1);
        Need(!board.Poll(),"EEPROM reload reset the configured master clock");
        Genesis::StallMasterCycles(1);Need(board.Poll(),"Reloaded EEPROM used the wrong busy duration");
        Genesis::SaveCartridge();
        if(board.wiring==SerialBoard::Acclaim32)
        {
            board.Lines(true,false);
            Genesis::M68KWriteMemoryWORD(0x200000,1);
            Need(Genesis::M68KReadMemoryBYTE(0x200001)==0x5a,"Acclaim word strobe did not select original ROM bytes");
            Genesis::M68KWriteMemoryWORD(0x200000,0);
            Need(Genesis::M68KReadMemoryBYTE(0x200001)==0x5a,"Acclaim EEPROM mux did not restore driven-low SDA");
            board.Lines(true,true);
            Need(Genesis::M68KReadMemoryBYTE(0x200001)==0x5b,"Acclaim byte strobes were mistaken for ROM-mux word writes");
        }
    }
}
void FailureAndRetry(const Sandbox& fixture)
{
    const auto directory=fixture.Path("failures");Directory(directory);
    const auto path=directory+"/game.bin";const auto save=path+".srm";
    const auto rom=RamImage(0x400,0x40f);
    Genesis::InsertCartridge(rom,path);Genesis::M68KWriteMemoryBYTE(0x400,0x12);Genesis::SaveCartridge();
    auto old=ReadFile(save);
    auto VerifyFailure=[&]()
    {
        Need(ReadFile(save)==old,"Failed atomic save changed the previous save");
        Need(Genesis::M68KReadMemoryBYTE(0x400)==0x34,"Failed save discarded dirty SRAM memory");
        NoTemporary(directory);
    };
    Genesis::M68KWriteMemoryBYTE(0x400,0x34);
#ifdef DRIVONIX_IO_FAULT_TESTS
    SaveFault::kind=SaveFault::Kind::Full;SaveFault::remaining=7;SaveFault::writes=0;
    const bool fullFailed=SaveFails();SaveFault::kind=SaveFault::Kind::None;
    Need(fullFailed&&SaveFault::writes>=2,"Partial-write ENOSPC simulation did not exercise the save failure path");
    VerifyFailure();
    SaveFault::kind=SaveFault::Kind::Rename;
    const bool renameFailed=SaveFails();SaveFault::kind=SaveFault::Kind::None;
    Need(renameFailed,"Save did not report atomic-replacement EACCES");VerifyFailure();
    auto replacement=rom;replacement[0x300]=0xc7;
    SaveFault::kind=SaveFault::Kind::Rename;
    bool switchFailed=false;
    try { Genesis::InsertCartridge(replacement,directory+"/replacement.bin"); }
    catch(const std::runtime_error&) { switchFailed=true; }
    SaveFault::kind=SaveFault::Kind::None;
    Need(switchFailed&&Genesis::M68KReadMemoryBYTE(0x300)==rom[0x300],"Failed save during cartridge switch discarded the active cartridge");
    VerifyFailure();
#endif
#ifndef _WIN32
    if(geteuid()!=0)
    {
        Need(chmod(directory.c_str(),0500)==0,"Cannot make fixture directory unwritable");
        const bool denied=SaveFails();
        Need(chmod(directory.c_str(),0700)==0,"Cannot restore fixture directory permissions");
        Need(denied,"Save succeeded in an unwritable fixture directory");VerifyFailure();
    }
#endif
    // Portable real filesystem failures exercise both native Save branches.
    const auto backup=directory+"/previous.srm";
    Need(std::rename(save.c_str(),backup.c_str())==0,"Cannot move owned save fixture");Directory(save);
    Need(SaveFails(),"Save incorrectly replaced a directory with a regular file");
    Need(ReadFile(backup)==old,"Failed replacement damaged the previous save fixture");
    Need(Genesis::M68KReadMemoryBYTE(0x400)==0x34,"Failed replacement lost dirty SRAM");NoTemporary(directory);
    RemoveDirectory(save);Need(std::rename(backup.c_str(),save.c_str())==0,"Cannot restore previous save fixture");
    const auto moved=directory+"-moved";
    Need(std::rename(directory.c_str(),moved.c_str())==0,"Cannot move owned fixture directory");WriteFile(directory,{0});
    Need(SaveFails(),"Save accepted a regular file as its parent directory");
    Need(ReadFile(moved+"/game.bin.srm")==old,"Failed temporary-file creation changed the previous save");
    Need(std::remove(directory.c_str())==0&&std::rename(moved.c_str(),directory.c_str())==0,"Cannot restore fixture parent directory");
    VerifyFailure();Genesis::SaveCartridge();
    auto latest=old;latest[0]=0x34;Need(ReadFile(save)==latest,"Save retry lost dirty bytes after the directory became writable");
    Genesis::InsertCartridge(rom,path);Need(Genesis::M68KReadMemoryBYTE(0x400)==0x34,"Retried SRAM save did not reload");

    const SerialBoard serial={"GM T-12046-00",128,0x25,0x200001,0x200001,1,SerialBoard::Sega,false};
    const auto serialROM=SerialImage(serial);const auto serialPath=directory+"/serial.bin";
    Genesis::InsertCartridge(serialROM,serialPath);serial.Write(serial.address,0x51);Genesis::SaveCartridge();
    const auto serialOld=ReadFile(serialPath+".eep");
    Genesis::StallMasterCycles((Genesis::MasterClock()+199)/200);serial.Write(serial.address,0xa6);
#ifdef DRIVONIX_IO_FAULT_TESTS
    SaveFault::kind=SaveFault::Kind::Rename;
    const bool serialFailed=SaveFails();SaveFault::kind=SaveFault::Kind::None;
    Need(serialFailed,"EEPROM replacement failure was not reported");
    Need(ReadFile(serialPath+".eep")==serialOld&&EEPROM::Dirty(),"Failed EEPROM save replaced old data or cleared dirty state");
    NoTemporary(directory);
#endif
    Genesis::SaveCartridge();Need(!EEPROM::Dirty(),"EEPROM retry did not clear dirty state");
    Genesis::InsertCartridge(serialROM,serialPath);
    Need(serial.Read(serial.address)==0xa6,"EEPROM retry/reload discarded dirty serial bytes");
}
void RejectedSaveLoads(const Sandbox& fixture)
{
    const auto activeROM=RamImage(0x400,0x40f);
    const auto activePath=fixture.Path("active.bin");
    Genesis::InsertCartridge(activeROM,activePath);
    Genesis::M68KWriteMemoryBYTE(0x400,0x47);Genesis::SaveCartridge();
    Genesis::M68KWriteMemoryBYTE(0xa130f1,3);
    const SerialBoard serial={"GM T-12046-00",128,0x25,0x200001,0x200001,1,SerialBoard::Sega,false};
    for(unsigned kind=0;kind<2;++kind)
    {
        auto candidate=kind ? SerialImage(serial):activeROM;candidate[0x300]=0xd1;
        const auto path=fixture.Path(kind ? "unreadable-serial.bin":"unreadable-sram.bin");
        const auto save=path+(kind ? ".eep":".srm");
        const std::vector<byte> original(kind ? 128:16,0x69);WriteFile(save,original);
#ifdef _WIN32
        HANDLE denied=CreateFileA(save.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        Need(denied!=INVALID_HANDLE_VALUE,"Cannot lock owned save fixture against reads");
#else
        if(geteuid()==0) continue; // Root bypasses ordinary file mode restrictions.
        Need(chmod(save.c_str(),0000)==0,"Cannot deny read permission on owned save fixture");
#endif
        bool rejected=false;
        try { Genesis::InsertCartridge(candidate,path); }
        catch(const std::runtime_error&) { rejected=true; }
#ifdef _WIN32
        Need(CloseHandle(denied)!=0,"Cannot unlock owned save fixture");
#else
        Need(chmod(save.c_str(),0600)==0,"Cannot restore read permission on owned save fixture");
#endif
        Need(rejected,"Unreadable existing save was treated as a missing save");
        Need(ReadFile(save)==original,"Rejected insertion changed the unreadable save");
        Need(Genesis::M68KReadMemoryBYTE(0x300)==activeROM[0x300]&&Genesis::M68KReadMemoryBYTE(0x400)==0x47,"Rejected insertion replaced the active ROM or SRAM");
        Genesis::M68KWriteMemoryBYTE(0x400,0x99);
        Need(Genesis::M68KReadMemoryBYTE(0x400)==0x47,"Rejected insertion discarded active SRAM protection");
    }
    const auto serialPath=fixture.Path("active-serial.bin");
    Genesis::InsertCartridge(SerialImage(serial),serialPath);
    serial.Write(serial.address,0x88);Genesis::SaveCartridge();
    Genesis::StallMasterCycles((Genesis::MasterClock()+199)/200);
    serial.Start();serial.Address(serial.address);Need(serial.Send(0x99),"Cannot stage an in-flight serial write");
    const auto invalid=fixture.Path("bad-size.bin");WriteFile(invalid+".srm",{1,2,3});
    bool rejected=false;
    try { Genesis::InsertCartridge(activeROM,invalid); }
    catch(const std::runtime_error&) { rejected=true; }
    Need(rejected&&EEPROM::Bytes().size()==128,"Malformed candidate save replaced the active serial profile");
    serial.Stop();Genesis::StallMasterCycles((Genesis::MasterClock()+199)/200);
    Need(serial.Read(serial.address)==0x99,"Rejected insertion discarded the active EEPROM transaction");
    Genesis::SaveCartridge();
    Need(ReadFile(serialPath+".eep")[serial.address]==0x99,"Rejected insertion changed the active EEPROM save path");
}
}

int main(int argc,char* argv[])
{
    try
    {
        Need(argc==2,"Expected an owned writable fixture directory");const Sandbox fixture(argv[1]);
        Genesis::AllocM68kMemory();Genesis::ResetM68kMemory();Genesis::Init();YM7101::Init();
        LoaderAndROM(fixture);SRAM(fixture);Banking(fixture);SerialPersistence(fixture);FailureAndRetry(fixture);RejectedSaveLoads(fixture);
        std::cout<<"Cartridge ROM preservation, SRAM lanes/protection, banks, EEPROM bus/reload clocks and atomic save retry passed\n";
        return 0;
    }
    catch(const std::exception& error)
    {
        std::cerr<<error.what()<<'\n';return 1;
    }
}
