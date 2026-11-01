#include "genesis/genesis.hpp"
#include "cathoderaytube/crt.hpp"
#include "platform/platform.hpp"
#include "platform/audiooutput.hpp"
#include "platform/settings.hpp"
#include "platform/archive.hpp"
#include "platform/controller.hpp"
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
    std::cout<<"Usage: drivonix [--config FILE | --portable] [--print-config-path] [--no-audio] [--archive-member NAME] [--list-archive] [--] [ROM]\n"
               "Run without a ROM path to open the file picker.\n"
               "--config FILE       Read/save this INI file instead of the per-user settings.\n"
               "--portable          Use config/drivonix.ini beside the executable.\n"
               "--print-config-path Print the active settings path without creating files.\n"
               "--archive-member NAME Select this exact ROM inside a ZIP/7z archive.\n"
               "--list-archive      List supported ROM members without opening a window.\n"
               "--no-audio          Disable host sound for this run without changing the INI.\n";
}
// Paths and member names use UTF-8 independently of the Windows ANSI code page.
std::string PathArgument(int index,const char* fallback) {
#ifdef _WIN32
    int count=0;
    wchar_t** arguments=CommandLineToArgvW(GetCommandLineW(),&count);
    if(!arguments) throw std::runtime_error("Cannot read Windows command line");
    if(index>=count) {LocalFree(arguments);throw std::runtime_error("Missing command-line argument");}
    const int length=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,arguments[index],-1,nullptr,0,nullptr,nullptr);
    if(length<=0) {LocalFree(arguments);throw std::runtime_error("Invalid Unicode argument");}
    std::string result(static_cast<std::size_t>(length),'\0');
    const int copied=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,arguments[index],-1,&result[0],length,nullptr,nullptr);
    LocalFree(arguments);
    if(copied!=length) throw std::runtime_error("Cannot convert argument to UTF-8");
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
        bool memberProvided=false,listArchive=false;
        std::string selected,configPath,member;
        for(int i=1;i<argc;++i) {
            const std::string arg=argv[i];
            if(!literal && (arg=="--help" || arg=="-h")) {Usage();return 0;}
            if(!literal && arg=="--") {literal=true;continue;}
            if(!literal && arg=="--no-audio") {noAudio=true;continue;}
            if(!literal && arg=="--portable") {portable=true;continue;}
            if(!literal && arg=="--print-config-path") {printConfig=true;continue;}
            if(!literal && arg=="--list-archive") {listArchive=true;continue;}
            if(!literal && arg=="--archive-member") {
                if(memberProvided || i+1>=argc) {Usage();return 2;}
                ++i;member=PathArgument(i,argv[i]);memberProvided=true;
                if(member.empty()) {Usage();return 2;}
                continue;
            }
            if(!literal && arg=="--config") {
                if(customConfig || i+1>=argc) {Usage();return 2;}
                ++i;configPath=PathArgument(i,argv[i]);customConfig=true;
                if(configPath.empty()) {Usage();return 2;}
                continue;
            }
            if(provided || (!literal && !arg.empty() && arg[0]=='-')) {Usage();return 2;}
            selected=PathArgument(i,argv[i]);provided=true;
        }
        if(customConfig && portable) {Usage();return 2;}
        if(listArchive && (!provided || memberProvided || printConfig)) {Usage();return 2;}
        if(provided && selected.empty()) throw std::runtime_error("ROM path is empty");
        if(listArchive) {
            for(const auto& entry: Archive::List(selected)) std::cout<<entry.name<<'\n';
            return 0;
        }
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
        if(Archive::IsArchive(romPath)) {
            if(!memberProvided) {
                std::vector<std::string> names;
                for(const auto& entry: Archive::List(romPath)) names.push_back(entry.name);
                member=Platform::SelectArchiveMember(romPath,names);
                if(member.empty()) return 0;
            }
            Genesis::SetRomName(Archive::SaveIdentity(romPath,member));
        } else {
            if(memberProvided) throw std::runtime_error("--archive-member requires a ZIP or 7z archive");
            Genesis::SetRomName(romPath);
        }
        RomLoader::Init();
        RomLoader::LoadRomFile(romPath,member);
        const auto cartridgeBytes=RomLoader::Bytes();
        const byte hardwareRegion=RomLoader::GetHardwareRegion();
        const std::string biosPath=Platform::DataFile("bios/drivonix.bios");
        RomLoader::LoadRomFile(biosPath);
        Genesis::SetHardwareRegion(hardwareRegion);
        Platform::CheckDisplay();
        CRT::Init(settings);
        Controller::Configure({settings.controllersEnabled,settings.controllerDeadzone,settings.controller1,settings.controller2});
        AudioOutput::ApplySettings(settings);
        Genesis::AllocM68kMemory();Genesis::ResetM68kMemory();
        Genesis::InsertCartridge(RomLoader::Bytes(),biosPath);
        Genesis::Init();GamePad::Init();M68k::Init();YM7101::Init();
        if(Genesis::Bios(hardwareRegion)) {
            RomLoader::LoadCartridgeImage(cartridgeBytes);
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
