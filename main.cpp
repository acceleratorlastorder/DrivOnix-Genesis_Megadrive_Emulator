#include "frontend/application.hpp"
#include <iostream>
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
               "Run without a ROM path to open the library and file picker.\n"
               "--config FILE       Read/save this INI file instead of the per-user settings.\n"
               "--portable          Use config/drivonix.ini beside the executable.\n"
               "--print-config-path Print the active settings path without creating files.\n"
               "--archive-member NAME Select this exact ROM inside a ZIP/7z archive.\n"
               "--list-archive      List supported ROM members without opening a window.\n"
               "--no-audio          Disable host sound for this run without changing the INI.\n"
               "--no-dialogs        Report errors to stderr and exit (for automation).\n";
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
    Frontend::Options options;
    try {
        bool literal=false,provided=false,printConfig=false,listArchive=false;
        std::string custom;
        for(int i=1;i<argc;++i){
            const std::string arg=argv[i];
            if(!literal&&(arg=="--help"||arg=="-h")){Usage();return 0;}
            if(!literal&&arg=="--"){literal=true;continue;}
            if(!literal&&arg=="--no-audio"){options.noAudio=true;continue;}
            if(!literal&&arg=="--no-dialogs"){options.noDialogs=true;continue;}
            if(!literal&&arg=="--portable"){options.portable=true;continue;}
            if(!literal&&arg=="--print-config-path"){printConfig=true;continue;}
            if(!literal&&arg=="--list-archive"){listArchive=true;continue;}
            if(!literal&&(arg=="--config"||arg=="--archive-member")){
                if(i+1>=argc||(arg=="--config"?options.customConfig:options.memberProvided)){Usage();return 2;}
                ++i;const auto value=PathArgument(i,argv[i]);if(value.empty()){Usage();return 2;}
                if(arg=="--config"){options.customConfig=true;custom=value;}else{options.memberProvided=true;options.member=value;}
                continue;
            }
            if(provided||(!literal&&!arg.empty()&&arg[0]=='-')){Usage();return 2;}
            options.romPath=PathArgument(i,argv[i]);provided=true;
        }
        if(options.customConfig&&options.portable){Usage();return 2;}
        if(listArchive&&(!provided||options.memberProvided||printConfig)){Usage();return 2;}
        if(provided&&options.romPath.empty())throw std::runtime_error("ROM path is empty");
        if(listArchive){for(const auto& entry:Archive::List(options.romPath))std::cout<<entry.name<<'\n';return 0;}
        options.configPath=Platform::SettingsPath(custom,options.portable);
        if(printConfig){std::cout<<options.configPath<<'\n';return 0;}
        return Frontend::RunApplication(options);
    }catch(const std::exception& error){
        std::cerr<<"drivonix: "<<error.what()<<'\n';
        if(!options.noDialogs){try{Platform::ShowError("DrivOnix",error.what());}catch(...){}}
        return 1;
    }
}
