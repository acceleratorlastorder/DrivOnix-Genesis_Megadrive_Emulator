#include "cartridge.hpp"
#include "eeprom.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <utility>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif
namespace {
#ifdef _WIN32
std::wstring WidePath(const std::string& path)
{
    if(path.find('\0')!=std::string::npos)throw std::runtime_error("Invalid NUL in cartridge save path");
    const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path.data(),int(path.size()),nullptr,0);
    if(!count)throw std::runtime_error("Invalid UTF-8 cartridge save path");
    std::wstring result(count,L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path.data(),int(path.size()),&result[0],count);
    return result;
}
#endif
void RemoveTemporary(const std::string& path)
{
#ifdef _WIN32
    DeleteFileW(WidePath(path).c_str());
#else
    std::remove(path.c_str());
#endif
}
std::vector<byte> image, ram;
std::array<unsigned, 8> banks;
dword ramStart=0, ramEnd=0;
bool singleLane=false, enabled=false, writeProtected=false, dirty=false;
std::string savePath;
EEPROM::Profile serialProfile=EEPROM::Profile::None;
dword BE32(const std::vector<byte>& bytes,unsigned at) { return (dword(bytes[at])<<24)|(dword(bytes[at+1])<<16)|(dword(bytes[at+2])<<8)|bytes[at+3]; }
bool Contains(dword address) {
    return enabled && !ram.empty() && address>=ramStart && address<=ramEnd && (!singleLane || ((address^ramStart)&1)==0);
}
std::vector<byte> ReadSave(const std::string& path,unsigned size)
{
    errno=0;
#ifdef _WIN32
    const auto wide=WidePath(path);
    std::unique_ptr<std::FILE,int(*)(std::FILE*)> input(_wfopen(wide.c_str(),L"rb"),std::fclose);
#else
    std::unique_ptr<std::FILE,int(*)(std::FILE*)> input(std::fopen(path.c_str(),"rb"),std::fclose);
#endif
    if(!input)
    {
        if(errno==ENOENT) return {};
        throw std::runtime_error("Cannot open cartridge save: "+path);
    }
    if(std::fseek(input.get(),0,SEEK_END)!=0||std::ftell(input.get())!=long(size))
        throw std::runtime_error("Incorrect cartridge save size: "+path);
    std::vector<byte> persisted(size);
    if(std::fseek(input.get(),0,SEEK_SET)!=0||std::fread(persisted.data(),1,size,input.get())!=size)
        throw std::runtime_error("Cannot read cartridge save: "+path);
    return persisted;
}
}
void Cartridge::Load(const std::vector<byte>& rom, const std::string& path) {
    Save();
    auto nextImage=rom;
    std::vector<byte> nextRam,serialBytes;
    const auto nextProfile=EEPROM::Detect(nextImage);
    std::string nextPath=path+(nextProfile==EEPROM::Profile::None ? ".srm":".eep");
    dword nextStart=0,nextEnd=0;
    bool nextSingleLane=false,nextEnabled=false;
    if(nextProfile!=EEPROM::Profile::None) {
        serialBytes=ReadSave(nextPath,EEPROM::Capacity(nextProfile));
    } else if(nextImage.size()>=0x200&&nextImage[0x1b0]=='R'&&nextImage[0x1b1]=='A') {
        nextStart=BE32(nextImage,0x1b4)&0xffffff;nextEnd=BE32(nextImage,0x1b8)&0xffffff;
        if(nextEnd<nextStart||nextEnd>=0x400000) throw std::runtime_error("Invalid cartridge save-RAM range");
        nextSingleLane=((nextStart^nextEnd)&1)==0;
        const dword size=(nextEnd-nextStart)/(nextSingleLane ? 2:1)+1;
        if(size>0x20000) throw std::runtime_error("Unsupported cartridge save-RAM size");
        nextRam=ReadSave(nextPath,size);
        if(nextRam.empty()) nextRam.assign(size,0xff);
        nextEnabled=nextImage.size()<=0x200000;
    }
    // Missing saves are new devices. Every other read/validation failure leaves
    // the previous cartridge, mapper, RAM and in-flight serial device intact.
    EEPROM::Init(nextProfile,serialBytes);
    image=std::move(nextImage);ram=std::move(nextRam);savePath=std::move(nextPath);
    serialProfile=nextProfile;ramStart=nextStart;ramEnd=nextEnd;
    singleLane=nextSingleLane;enabled=nextEnabled;writeProtected=false;dirty=false;
    for(unsigned i=0;i<8;++i) banks[i]=i;
}
byte Cartridge::Read(dword address) {
    address&=0xffffff;
    if (Contains(address)) return ram[(address-ramStart)/(singleLane ? 2:1)];
    if (address>=0x400000) return 0xff;
    const std::size_t offset=std::size_t(banks[address>>19])*0x80000+(address&0x7ffff);
    const byte value=offset<image.size() ? image[offset]:0xff;
    return EEPROM::IsMappedRead(address) ? EEPROM::Read8(address,value):value;
}
void Cartridge::Write(dword address, byte value) {
    address&=0xffffff;
    if (EEPROM::IsMappedWrite(address)) { EEPROM::Write8(address,value);return; }
    if (address==0xa130f1) { enabled=(value&1)!=0; writeProtected=(value&2)!=0; return; }
    if (address>=0xa130f3 && address<=0xa130ff && (address&1)) {
        banks[(address-0xa130f1)/2]=value&0x3f; return;
    }
    if (Contains(address) && !writeProtected) {
        auto& target=ram[(address-ramStart)/(singleLane ? 2:1)];
        if (target!=value) { target=value; dirty=true; }
    }
}
void Cartridge::WriteWord(dword address,word value) {
    if(EEPROM::IsMappedWrite(address) || EEPROM::IsMappedWrite(address+1)) EEPROM::Write16(address,value);
    else {Write(address,byte(value>>8));Write(address+1,byte(value));}
}
bool Cartridge::HasSaveRAM() { return !ram.empty(); }
void Cartridge::Save() {
    const bool serial=serialProfile!=EEPROM::Profile::None;
    const auto& bytes=serial ? EEPROM::Bytes():ram;
    if (!(serial ? EEPROM::Dirty():dirty) || bytes.empty()) return;
    const std::string temporary=savePath+".tmp."+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
#ifdef _WIN32
    HANDLE file=CreateFileW(WidePath(temporary).c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,nullptr);
    if(file==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot create save-RAM temporary file: "+savePath);
    DWORD written=0;
    const bool complete=WriteFile(file,bytes.data(),DWORD(bytes.size()),&written,nullptr)!=0 && written==bytes.size() && FlushFileBuffers(file)!=0;
    const bool closed=CloseHandle(file)!=0;
#else
    const int file=open(temporary.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600);
    if(file<0) throw std::runtime_error("Cannot create save-RAM temporary file: "+savePath);
    std::size_t offset=0;
    while(offset<bytes.size()) {
        const auto written=write(file,bytes.data()+offset,bytes.size()-offset);
        if(written<=0) break;
        offset+=static_cast<std::size_t>(written);
    }
    const bool complete=offset==bytes.size() && fsync(file)==0;
    const bool closed=close(file)==0;
#endif
    if(!complete || !closed) {RemoveTemporary(temporary);throw std::runtime_error("Cannot flush save-RAM: "+savePath);}
#ifdef _WIN32
    const bool replaced=MoveFileExW(WidePath(temporary).c_str(),WidePath(savePath).c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    const bool replaced=std::rename(temporary.c_str(),savePath.c_str())==0;
#endif
    if (!replaced) { RemoveTemporary(temporary); throw std::runtime_error("Cannot replace save-RAM: "+savePath); }
#ifndef _WIN32
    const auto slash=savePath.find_last_of('/');
    const std::string directory=slash==std::string::npos ? ".":(slash==0 ? "/":savePath.substr(0,slash));
    const int directoryFile=open(directory.c_str(),O_RDONLY|O_DIRECTORY);
    if(directoryFile<0) throw std::runtime_error("Cannot open save directory for synchronization");
    const bool directorySynced=fsync(directoryFile)==0;close(directoryFile);
    if(!directorySynced) throw std::runtime_error("Cannot synchronize save directory");
#endif
    dirty=false;EEPROM::ClearDirty();
}
