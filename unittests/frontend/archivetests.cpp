#include "../../platform/archive.hpp"
#include "../../platform/process.hpp"
#include "../../platform/platform.hpp"
#include "../../genesis/romloader/romloader.hpp"
#include "../../genesis/cartridge/cartridge.hpp"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace
{
void Require(bool condition,const char* message)
{
    if(!condition)throw std::runtime_error(message);
}
std::vector<std::string> Arguments(int argc,char** argv)
{
#ifdef _WIN32
    (void)argc;(void)argv;
    int count=0;auto arguments=CommandLineToArgvW(GetCommandLineW(),&count);
    if(!arguments)throw std::runtime_error("Cannot read Unicode test arguments");
    std::vector<std::string> result;
    for(int i=0;i<count;++i)
    {
        const int size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,arguments[i],-1,nullptr,0,nullptr,nullptr);
        if(!size){LocalFree(arguments);throw std::runtime_error("Invalid test argument");}
        std::string value(size,'\0');WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,arguments[i],-1,&value[0],size,nullptr,nullptr);
        value.pop_back();result.push_back(value);
    }
    LocalFree(arguments);return result;
#else
    return std::vector<std::string>(argv,argv+argc);
#endif
}
std::vector<byte> Read(const std::string& path)
{
#ifdef _WIN32
    const int wideCount=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path.data(),int(path.size()),nullptr,0);
    Require(wideCount>0,"Invalid expected ROM path");std::wstring wide(wideCount,L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path.data(),int(path.size()),&wide[0],wideCount);
    std::unique_ptr<std::FILE,int(*)(std::FILE*)> file(_wfopen(wide.c_str(),L"rb"),std::fclose);
#else
    std::unique_ptr<std::FILE,int(*)(std::FILE*)> file(std::fopen(path.c_str(),"rb"),std::fclose);
#endif
    Require(bool(file),"Cannot open expected ROM");
    Require(std::fseek(file.get(),0,SEEK_END)==0,"Cannot size expected ROM");
    const long size=std::ftell(file.get());Require(size>=0&&size<0x900000,"Invalid expected ROM size");
    std::vector<byte> bytes(static_cast<std::size_t>(size));
    Require(std::fseek(file.get(),0,SEEK_SET)==0&&std::fread(bytes.data(),1,bytes.size(),file.get())==bytes.size(),"Cannot read expected ROM");
    return bytes;
}
void Processes(const std::string& python)
{
    const std::string argument=u8"space \"quote\" 日本語 ';&$() [*?] \\trailing\\";
    auto result=Process::Run({python,"-c","import sys;sys.stdout.buffer.write(sys.argv[1].encode('utf-8'))",argument},4096);
    Require(result.exitCode==0&&std::string(result.output.begin(),result.output.end())==argument,"Process argument quoting or Unicode changed data");
    result=Process::Run({python,"-c","import sys;sys.stderr.buffer.write(b'e'*200000);sys.stdout.buffer.write(b'a'*200000)"},200000);
    Require(result.exitCode==0&&result.output.size()==200000&&result.error.size()==65536,"Process pipe backpressure or stderr bound");
    Require(std::all_of(result.output.begin(),result.output.end(),[](byte value){return value=='a';}),"stderr leaked into binary stdout");
    result=Process::Run({python,"-c","import sys;sys.exit(23)"},100);
    Require(result.exitCode==23,"Process exit status lost");
    result=Process::Run({python,"-c","import sys;sys.stdout.buffer.write(sys.stdin.buffer.read())"},100);
    Require(result.exitCode==0&&result.output.empty(),"Child stdin was not closed");
    bool rejected=false;
    try{Process::Run({python,"-c","import time;time.sleep(5)"},100,100);}catch(const std::runtime_error& e){rejected=std::string(e.what()).find("timed out")!=std::string::npos;}
    Require(rejected,"Process timeout did not terminate child");
    rejected=false;
    try{Process::Run({python,"-c","import sys;sys.stdout.buffer.write(b'a'*200000)"},1000);}catch(const std::runtime_error& e){rejected=std::string(e.what()).find("allowed size")!=std::string::npos;}
    Require(rejected,"Process stdout limit did not terminate child");
    result=Process::Run({python,"-c","a=bytearray(600*1024*1024)"},1000);
    Require(result.exitCode!=0,"Archive child memory limit was not applied");
}
void Saves(const std::string& archive)
{
    const auto first=Archive::SaveIdentity(archive,u8"nested/日本語 sonic.bin");
    const auto second=Archive::SaveIdentity(archive,"other sonic.bin");
    Require(first!=second&&first==Archive::SaveIdentity(archive,u8"nested/日本語 sonic.bin"),"Archive save identities collide or change");
    Require(first==Archive::SaveIdentity(archive,u8"nested\\日本語 sonic.bin"),"Archive save identity depends on platform path separators");
    std::vector<byte> rom(0x400,0);
    rom[0x1b0]='R';rom[0x1b1]='A';
    rom[0x1b4]=0;rom[0x1b5]=0x20;rom[0x1b6]=0;rom[0x1b7]=1;
    rom[0x1b8]=0;rom[0x1b9]=0x20;rom[0x1ba]=0;rom[0x1bb]=0xff;
    Cartridge::Load(rom,first);Cartridge::Write(0x200001,0x35);Cartridge::Save();
    Cartridge::Load(rom,second);Require(Cartridge::Read(0x200001)==0xff,"Archive members shared save RAM");
    Cartridge::Write(0x200001,0x79);Cartridge::Save();
    Cartridge::Load(rom,first);Require(Cartridge::Read(0x200001)==0x35,"First archive member did not reload save RAM");
    Cartridge::Load(rom,second);Require(Cartridge::Read(0x200001)==0x79,"Second archive member did not reload save RAM");
}
}

int main(int argc,char** argv)
{
    try
    {
        const auto args=Arguments(argc,argv);Require(args.size()>=3,"Expected archive test mode and path");
        if(args[1]=="datafile") {
            const auto path=Platform::DataFile("bios/drivonix.bios");
            RomLoader::LoadRomFile(path);std::cout<<path<<'\n';
        }
        else if(args[1]=="list")
            for(const auto& entry:Archive::List(args[2]))std::cout<<entry.name<<'\t'<<entry.size<<'\n';
        else if(args[1]=="read"||args[1]=="load")
        {
            Require(args.size()==5,"Expected archive, member and baseline ROM");
            if(args[1]=="read")Require(Archive::Read(args[2],args[3])==Read(args[4]),"Extracted ROM bytes differ");
            else
            {
                RomLoader::LoadRomFile(args[4]);const auto expected=RomLoader::Bytes();
                const auto region=RomLoader::GetHardwareRegion();
                RomLoader::LoadRomFile(args[2],args[3]);Require(RomLoader::Bytes()==expected,"Archived ROM decoding differs from raw ROM");
                Require(RomLoader::GetHardwareRegion()==region,"Archived ROM region changed");
                RomLoader::LoadCartridgeImage(expected);Require(RomLoader::Bytes()==expected&&RomLoader::GetHardwareRegion()==region,"Cached image restore changed cartridge bytes or region");
                bool rejected=false;try{RomLoader::LoadCartridgeImage(std::vector<byte>(128));}catch(const std::runtime_error&){rejected=true;}
                Require(rejected,"Cached image accepted an incomplete header");
            }
        }
        else if(args[1]=="process")Processes(args[2]);
        else if(args[1]=="save")Saves(args[2]);
        else throw std::runtime_error("Unknown archive test mode");
        return 0;
    }
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
