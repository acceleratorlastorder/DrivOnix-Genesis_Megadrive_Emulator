#include "genesis/genesis.hpp"
#include "cathoderaytube/crt.hpp"
#include "platform/platform.hpp"
#include "platform/audiooutput.hpp"
#include "platform/settings.hpp"
#include <exception>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace {
void Usage() {
    std::cout<<"Usage: drivonix [--config FILE | --portable] [--print-config-path] [--no-audio] [--] [ROM]\n"
               "Run without a ROM path to open the file picker.\n"
               "--config FILE       Read/save this INI file instead of the per-user settings.\n"
               "--portable          Use config/drivonix.ini beside the executable.\n"
               "--print-config-path Print the active settings path without creating files.\n"
               "--no-audio          Disable host sound for this run without changing the INI.\n";
}
// Settings paths are UTF-8, including Windows command lines. Keep the existing
// ROM-loading argument convention separate from the new Unicode settings I/O.
std::string ConfigArgument(int index,const char* fallback) {
#ifdef _WIN32
    int count=0;
    wchar_t** arguments=CommandLineToArgvW(GetCommandLineW(),&count);
    if(!arguments) throw std::runtime_error("Cannot read Windows settings argument");
    if(index>=count) {LocalFree(arguments);throw std::runtime_error("Missing settings argument");}
    const int length=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,arguments[index],-1,nullptr,0,nullptr,nullptr);
    if(length<=0) {LocalFree(arguments);throw std::runtime_error("Invalid Unicode settings path");}
    std::string result(static_cast<std::size_t>(length),'\0');
    const int copied=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,arguments[index],-1,&result[0],length,nullptr,nullptr);
    LocalFree(arguments);
    if(copied!=length) throw std::runtime_error("Cannot convert settings path to UTF-8");
    result.pop_back();
    (void)fallback;
    return result;
#else
    (void)index;
    return fallback;
#endif
}
}

int main(int argc,char* argv[]) {
    try {
        bool noAudio=false,literal=false,provided=false,portable=false,printConfig=false,customConfig=false;
        std::string selected,configPath;
        for(int i=1;i<argc;++i) {
            const std::string arg=argv[i];
            if(!literal && (arg=="--help" || arg=="-h")) {Usage();return 0;}
            if(!literal && arg=="--") {literal=true;continue;}
            if(!literal && arg=="--no-audio") {noAudio=true;continue;}
            if(!literal && arg=="--portable") {portable=true;continue;}
            if(!literal && arg=="--print-config-path") {printConfig=true;continue;}
            if(!literal && arg=="--config") {
                if(customConfig || i+1>=argc) {Usage();return 2;}
                ++i;configPath=ConfigArgument(i,argv[i]);customConfig=true;
                if(configPath.empty()) {Usage();return 2;}
                continue;
            }
            if(provided || (!literal && !arg.empty() && arg[0]=='-')) {Usage();return 2;}
            selected=arg;provided=true;
        }
        if(customConfig && portable) {Usage();return 2;}
        if(provided && selected.empty()) throw std::runtime_error("ROM path is empty");
        configPath=Platform::SettingsPath(configPath,portable);
        if(printConfig) {std::cout<<configPath<<'\n';return 0;}

        auto document=Settings::Document::Load(configPath);
        if(!document.Exists()) {
            if(!customConfig) {
                auto values=document.Get();std::string legacyPath;
                if(Platform::ImportLegacySettings(values,legacyPath)) {
                    document.Apply(values);
                    std::cerr<<"drivonix: imported settings from "<<legacyPath<<'\n';
                }
            }
            Platform::EnsureSettingsDirectory(configPath);
            document.Save();
        }
        auto settings=document.Get();
        if(noAudio) settings.audioEnabled=false;
        const std::string romPath=provided ? selected:Platform::SelectRom();
        if(romPath.empty()) return 0;
        Genesis::SetRomName(romPath);
        RomLoader::Init();
        RomLoader::LoadRomFile(Genesis::GetRomName());
        const byte hardwareRegion=RomLoader::GetHardwareRegion();
        const std::string biosPath=Platform::DataFile("bios/drivonix.bios");
        RomLoader::LoadRomFile(biosPath);
        Genesis::SetHardwareRegion(hardwareRegion);
        Platform::CheckDisplay();
        CRT::Init(settings);
        AudioOutput::ApplySettings(settings);
        Genesis::AllocM68kMemory();Genesis::ResetM68kMemory();
        Genesis::InsertCartridge(RomLoader::Bytes(),biosPath);
        Genesis::Init();GamePad::Init();M68k::Init();YM7101::Init();
        if(Genesis::Bios(hardwareRegion)) {
            RomLoader::Init();RomLoader::LoadRomFile(Genesis::GetRomName());
            Genesis::SetHardwareRegion(hardwareRegion);RomLoader::PrintRomHeader();
            Genesis::ResetM68kMemory();Genesis::InsertCartridge();
            Genesis::Init();GamePad::Init();M68k::Init();YM7101::Init();Genesis::Run();
        }
        Genesis::SaveCartridge();AudioOutput::Shutdown();CRT::Shutdown();return 0;
    } catch(const std::exception& error) {
        try {Genesis::SaveCartridge();}
        catch(const std::exception& saveError) {std::cerr<<"drivonix save: "<<saveError.what()<<'\n';}
        AudioOutput::Shutdown();CRT::Shutdown();std::cerr<<"drivonix: "<<error.what()<<'\n';return 1;
    }
}
