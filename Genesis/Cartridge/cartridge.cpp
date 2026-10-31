#include "cartridge.hpp"
#include "eeprom.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <stdexcept>
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
std::vector<byte> image, ram;
std::array<unsigned, 8> banks;
dword ramStart=0, ramEnd=0;
bool singleLane=false, enabled=false, writeProtected=false, dirty=false;
std::string savePath;
EEPROM::Profile serialProfile=EEPROM::Profile::None;
dword BE32(unsigned at) { return (dword(image[at])<<24)|(dword(image[at+1])<<16)|(dword(image[at+2])<<8)|image[at+3]; }
bool Contains(dword address) {
    return enabled && !ram.empty() && address>=ramStart && address<=ramEnd && (!singleLane || ((address^ramStart)&1)==0);
}
}
void Cartridge::Load(const std::vector<byte>& rom, const std::string& path) {
    Save();
    image=rom; ram.clear(); dirty=false; enabled=false; writeProtected=false;
    for (unsigned i=0;i<8;++i) banks[i]=i;
    serialProfile=EEPROM::Detect(rom);
    EEPROM::Init(serialProfile);
    savePath=path+(serialProfile==EEPROM::Profile::None ? ".srm":".eep");
    if(serialProfile!=EEPROM::Profile::None) {
        std::ifstream input(savePath,std::ios::binary|std::ios::ate);
        if(input) {
            const auto length=input.tellg();
            if(length!=std::streamoff(EEPROM::Bytes().size())) throw std::runtime_error("Incorrect EEPROM save size: "+savePath);
            std::vector<byte> persisted(static_cast<std::size_t>(length));input.seekg(0);
            if(!input.read(reinterpret_cast<char*>(persisted.data()),persisted.size())) throw std::runtime_error("Cannot read EEPROM save: "+savePath);
            EEPROM::Init(serialProfile,persisted);
        }
        return;
    }
    if (image.size()<0x200 || image[0x1b0]!='R' || image[0x1b1]!='A') return;
    ramStart=BE32(0x1b4)&0xffffff; ramEnd=BE32(0x1b8)&0xffffff;
    if (ramEnd<ramStart || ramEnd>=0x400000) throw std::runtime_error("Invalid cartridge save-RAM range");
    singleLane=((ramStart^ramEnd)&1)==0;
    const dword size=(ramEnd-ramStart)/(singleLane ? 2:1)+1;
    if (size>0x20000) throw std::runtime_error("Unsupported cartridge save-RAM size");
    ram.assign(size,0xff);
    enabled=image.size()<=0x200000;
    std::ifstream input(savePath,std::ios::binary|std::ios::ate);
    if (input) {
        if (input.tellg()!=std::streamoff(ram.size())) throw std::runtime_error("Incorrect save-RAM file size: "+savePath);
        input.seekg(0);
        if (!input.read(reinterpret_cast<char*>(ram.data()),ram.size())) throw std::runtime_error("Cannot read save-RAM: "+savePath);
    }
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
    HANDLE file=CreateFileA(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,nullptr);
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
    if(!complete || !closed) {std::remove(temporary.c_str());throw std::runtime_error("Cannot flush save-RAM: "+savePath);}
#ifdef _WIN32
    const bool replaced=MoveFileExA(temporary.c_str(),savePath.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    const bool replaced=std::rename(temporary.c_str(),savePath.c_str())==0;
#endif
    if (!replaced) { std::remove(temporary.c_str()); throw std::runtime_error("Cannot replace save-RAM: "+savePath); }
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
