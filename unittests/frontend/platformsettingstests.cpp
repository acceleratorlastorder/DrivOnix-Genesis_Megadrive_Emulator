#include "../../platform/platform.hpp"
#include "../../platform/settings.hpp"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
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
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
void Need(bool pass,const char* why) { if(!pass) throw std::runtime_error(why); }
std::string Join(const std::string& base,const std::string& child) { return base+"/"+child; }
std::string Slashes(std::string path) { for(auto& c:path) if(c=='\\') c='/';return path; }
#ifdef _WIN32
std::wstring Wide(const std::string& text)
{
    const auto count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.c_str(),-1,nullptr,0);
    Need(count>0,"Cannot encode Windows fixture path");
    std::vector<wchar_t> result(std::size_t(count),L'\0');
    Need(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.c_str(),-1,result.data(),count)!=0,"Cannot encode Windows fixture path");
    return result.data();
}
std::string Utf8(const std::wstring& text)
{
    const auto count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.c_str(),-1,nullptr,0,nullptr,nullptr);
    Need(count>0,"Cannot decode Windows fixture path");
    std::vector<char> result(std::size_t(count),'\0');
    Need(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.c_str(),-1,result.data(),count,nullptr,nullptr)!=0,"Cannot decode Windows fixture path");
    return result.data();
}
std::string Current()
{
    const auto count=GetCurrentDirectoryW(0,nullptr);Need(count>0,"Cannot read fixture current directory");
    std::vector<wchar_t> buffer(count);Need(GetCurrentDirectoryW(count,buffer.data())!=0,"Cannot read fixture current directory");
    return Utf8(buffer.data());
}
void Change(const std::string& path) { Need(SetCurrentDirectoryW(Wide(path).c_str())!=0,"Cannot change fixture current directory"); }
void Write(const std::string& path,const std::string& bytes)
{
    HANDLE file=CreateFileW(Wide(path).c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    Need(file!=INVALID_HANDLE_VALUE,"Cannot create settings fixture");
    DWORD written=0;const bool okay=WriteFile(file,bytes.data(),DWORD(bytes.size()),&written,nullptr)!=0 && written==bytes.size();
    CloseHandle(file);Need(okay,"Cannot write settings fixture");
}
std::string Read(const std::string& path)
{
    HANDLE file=CreateFileW(Wide(path).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    Need(file!=INVALID_HANDLE_VALUE,"Cannot read settings fixture");
    char buffer[4096];DWORD size=0;const bool okay=ReadFile(file,buffer,sizeof(buffer),&size,nullptr)!=0;
    CloseHandle(file);Need(okay,"Cannot read settings fixture");return std::string(buffer,size);
}
void RemoveTree(const std::string& path)
{
    WIN32_FIND_DATAW item;HANDLE search=FindFirstFileW(Wide(path+"/*").c_str(),&item);
    if(search!=INVALID_HANDLE_VALUE)
    {
        do
        {
            const std::wstring name=item.cFileName;if(name==L"." || name==L"..") continue;
            const auto child=Join(path,Utf8(name));
            if(item.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) RemoveTree(child);
            else Need(DeleteFileW(Wide(child).c_str())!=0,"Cannot remove settings fixture file");
        } while(FindNextFileW(search,&item));
        FindClose(search);
    }
    Need(RemoveDirectoryW(Wide(path).c_str())!=0,"Cannot remove settings fixture directory");
}
struct Environment {
    std::wstring name,old;bool existed;
    explicit Environment(const wchar_t* key):name(key)
    {
        const auto count=GetEnvironmentVariableW(key,nullptr,0);existed=count!=0;
        if(existed) {std::vector<wchar_t> value(count);GetEnvironmentVariableW(key,value.data(),count);old=value.data();}
    }
    void Set(const std::string& value) {Need(SetEnvironmentVariableW(name.c_str(),Wide(value).c_str())!=0,"Cannot set settings fixture environment");}
    void Unset() {Need(SetEnvironmentVariableW(name.c_str(),nullptr)!=0,"Cannot clear settings fixture environment");}
    ~Environment() {SetEnvironmentVariableW(name.c_str(),existed ? old.c_str():nullptr);}
};
#else
std::string Current()
{
    std::vector<char> buffer(256);
    while(!getcwd(buffer.data(),buffer.size())) {Need(errno==ERANGE,"Cannot read fixture current directory");buffer.resize(buffer.size()*2);}
    return buffer.data();
}
void Change(const std::string& path) { Need(chdir(path.c_str())==0,"Cannot change fixture current directory"); }
void Write(const std::string& path,const std::string& bytes) {std::ofstream out(path,std::ios::binary);out<<bytes;Need(out.good(),"Cannot write settings fixture");}
std::string Read(const std::string& path) {std::ifstream in(path,std::ios::binary);Need(in.good(),"Cannot read settings fixture");return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};}
void RemoveTree(const std::string& path)
{
    DIR* directory=opendir(path.c_str());Need(directory!=nullptr,"Cannot inspect settings fixture directory");
    while(auto* entry=readdir(directory))
    {
        const std::string name=entry->d_name;if(name=="." || name=="..") continue;
        const auto child=Join(path,name);struct stat info={};Need(lstat(child.c_str(),&info)==0,"Cannot inspect settings fixture file");
        if(S_ISDIR(info.st_mode)) RemoveTree(child);else Need(unlink(child.c_str())==0,"Cannot remove settings fixture file");
    }
    closedir(directory);Need(rmdir(path.c_str())==0,"Cannot remove settings fixture directory");
}
struct Environment {
    std::string name,old;bool existed;
    explicit Environment(const char* key):name(key) {const auto* value=std::getenv(key);existed=value!=nullptr;if(value) old=value;}
    void Set(const std::string& value) {Need(setenv(name.c_str(),value.c_str(),1)==0,"Cannot set settings fixture environment");}
    void Unset() {Need(unsetenv(name.c_str())==0,"Cannot clear settings fixture environment");}
    ~Environment() {if(existed) setenv(name.c_str(),old.c_str(),1);else unsetenv(name.c_str());}
};
#endif
struct WorkingDirectory {
    std::string original=Current();
    ~WorkingDirectory() {
#ifdef _WIN32
        SetCurrentDirectoryW(Wide(original).c_str());
#else
        chdir(original.c_str());
#endif
    }
};
struct Fixture {
    std::string path;
    explicit Fixture(const std::string& parent)
    {
        const auto stem=std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        for(unsigned attempt=0;attempt<100;++attempt)
        {
            const auto candidate=Join(parent,"platform-settings-"+stem+"-"+std::to_string(attempt));
#ifdef _WIN32
            if(CreateDirectoryW(Wide(candidate).c_str(),nullptr)) {path=candidate;break;}
            Need(GetLastError()==ERROR_ALREADY_EXISTS,"Cannot create settings fixture root");
#else
            if(mkdir(candidate.c_str(),0700)==0) {path=candidate;break;}
            Need(errno==EEXIST,"Cannot create settings fixture root");
#endif
        }
        Need(!path.empty(),"No unique settings fixture directory");
    }
    ~Fixture() {try {RemoveTree(path);} catch(...) {}}
};
template<typename Function> std::string Failure(Function action)
{
    try {action();} catch(const std::exception& error) {return error.what();}
    throw std::runtime_error("Expected settings path/import rejection");
}
void Directory(const std::string& path) {Platform::EnsureSettingsDirectory(Join(path,"not-created.ini"));}

void Paths(const std::string& root)
{
    WorkingDirectory cwd;Change(root);
    const auto current=Current();
    Need(Slashes(Platform::SettingsPath("relative/settings.ini",false))==Slashes(Join(current,"relative/settings.ini")),"Explicit relative settings path did not resolve from working directory");
    Need(Failure([]{Platform::SettingsPath("explicit.ini",true);}).find("--config")!=std::string::npos,"Conflicting settings modes need an actionable error");
    const auto portable=Slashes(Platform::SettingsPath("",true));
    const std::string suffix="/config/drivonix.ini";
    Need(portable.size()>suffix.size() && portable.compare(portable.size()-suffix.size(),suffix.size(),suffix)==0,"Portable settings must use lowercase executable-adjacent config path");
    const auto unicode=Join(root,u8"r\u00e9glages_\u8a2d\u5b9a");
    Platform::EnsureSettingsDirectory(Join(unicode,"nested/drivonix.ini"));
#ifdef _WIN32
    const auto attributes=GetFileAttributesW(Wide(Join(unicode,"nested")).c_str());
    Need(attributes!=INVALID_FILE_ATTRIBUTES && (attributes&FILE_ATTRIBUTE_DIRECTORY),"Unicode Windows settings directory was not created");
    Environment local(L"LOCALAPPDATA");local.Set(unicode);
    Need(Slashes(Platform::SettingsPath("",false))==Slashes(Join(unicode,"drivonix/drivonix.ini")),"Unicode LOCALAPPDATA did not survive settings path resolution");
    local.Set("relative-appdata");
    const auto fallback=Slashes(Platform::SettingsPath("",false));
    Need(fallback.find("relative-appdata")==std::string::npos && fallback!=portable,"Relative LOCALAPPDATA must fall back to the known folder");
    local.Unset();Need(Slashes(Platform::SettingsPath("",false))==fallback,"Unset LOCALAPPDATA fallback differs from known folder");
#else
    Environment xdg("XDG_CONFIG_HOME"),home("HOME");
    xdg.Set(Join(root,"xdg"));home.Set(Join(root,"home"));
    Need(Platform::SettingsPath("",false)==Join(root,"xdg/drivonix/drivonix.ini"),"Absolute XDG config home not selected");
    xdg.Set("relative-xdg");Need(Platform::SettingsPath("",false)==Join(root,"home/.config/drivonix/drivonix.ini"),"Relative XDG_CONFIG_HOME must be ignored");
    xdg.Unset();Need(Platform::SettingsPath("",false)==Join(root,"home/.config/drivonix/drivonix.ini"),"HOME configuration fallback differs");
    home.Unset();Need(Failure([]{Platform::SettingsPath("",false);}).find("--config")!=std::string::npos,"Missing HOME needs explicit-config guidance");
    Need(Platform::SettingsPath("explicit.ini",false)==Join(current,"explicit.ini"),"An explicit path must work without HOME");
    const auto preserved=Join(root,"existing");Need(mkdir(preserved.c_str(),0755)==0,"Cannot create permission fixture");Need(chmod(preserved.c_str(),0755)==0,"Cannot set permission fixture");
    Platform::EnsureSettingsDirectory(Join(preserved,"private/nested/config.ini"));
    struct stat info={};Need(stat(preserved.c_str(),&info)==0 && (info.st_mode&0777)==0755,"Existing directory permissions changed");
    Need(stat(Join(preserved,"private").c_str(),&info)==0 && (info.st_mode&0777)==0700,"New settings directory must be private");
#endif
    Write(Join(root,"blocked"),"original");
    Failure([&]{Platform::EnsureSettingsDirectory(Join(root,"blocked/settings.ini"));});
    Need(Read(Join(root,"blocked"))=="original","Directory creation damaged an existing file");
}

void Legacy(const std::string& root)
{
    WorkingDirectory cwd;
    const auto requested=Join(root,u8"legacy_\u8a2d\u5b9a");Directory(Join(requested,"config"));Change(requested);
    const auto directory=Current();
    const auto path=Join(directory,"config/config.txt");
    for(unsigned screen=1;screen<=3;++screen)
    {
        const auto text="/* ScreenSize=3; ignored */\r\n  ScreenSize = "+std::to_string(screen)+"; // chosen\r\nScreenSize=9; ignored second assignment\r\n";
        Write(path,text);Settings::Values values;values.scale=8;values.fullscreen=true;values.audioEnabled=false;values.volume=37;std::string source="before";
        Need(Platform::ImportLegacySettings(values,source),"Legacy configuration was not imported");
        Need(values.scale==(screen==2 ? 2u:1u) && values.fullscreen==(screen==3),"Legacy screen-size mapping changed");
        Need(!values.audioEnabled && values.volume==37,"Legacy import changed unrelated audio settings");
        Need(Slashes(source)==Slashes(path),"Legacy import did not report its actual source");
        Need(Read(path)==text,"Legacy import modified the original file");
    }
#ifndef _WIN32
    Directory(Join(directory,"Config"));Write(Join(directory,"Config/config.txt"),"ScreenSize=1;\n");
    Write(path,"ScreenSize=2;\n");Settings::Values preferred;std::string source;
    Need(Platform::ImportLegacySettings(preferred,source) && preferred.scale==2,"Lowercase legacy candidate must have precedence");
    Need(unlink(path.c_str())==0,"Cannot remove lowercase legacy fixture");
    Need(Platform::ImportLegacySettings(preferred,source) && preferred.scale==1
         && source==Join(directory,"Config/config.txt"),"Old capitalized legacy directory was not imported");
#endif
    Write(path,"// comment\nScreenSize=9;\n");Settings::Values before;before.scale=7;before.volume=31;std::string unchanged="unchanged";
    const auto message=Failure([&]{Platform::ImportLegacySettings(before,unchanged);});
    Need(message.find("line 2")!=std::string::npos && message.find("ScreenSize")!=std::string::npos,"Invalid legacy value needs path/line/key context");
    Need(before.scale==7 && before.volume==31 && unchanged=="unchanged","Failed import changed active settings or source output");
    Write(path,"ScreenSize=2;\n");
#ifdef _WIN32
    HANDLE lock=CreateFileW(Wide(path).c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    Need(lock!=INVALID_HANDLE_VALUE,"Cannot lock legacy settings fixture");
    bool rejected=false;try {Platform::ImportLegacySettings(before,unchanged);} catch(const std::exception&) {rejected=true;}
    CloseHandle(lock);Need(rejected,"Unreadable legacy settings treated as missing");
#else
    if(geteuid()!=0)
    {
        Need(chmod(path.c_str(),0000)==0,"Cannot deny legacy fixture read permission");
        bool rejected=false;try {Platform::ImportLegacySettings(before,unchanged);} catch(const std::exception&) {rejected=true;}
        Need(chmod(path.c_str(),0600)==0,"Cannot restore legacy fixture permissions");
        Need(rejected,"Unreadable legacy settings treated as missing");
    }
#endif
    Need(before.scale==7 && unchanged=="unchanged","Unreadable import changed active settings");
    const auto blocked=Join(root,"blocked-legacy");Directory(blocked);Write(Join(blocked,"config"),"preserved");Change(blocked);
    Failure([&]{Platform::ImportLegacySettings(before,unchanged);});
    Need(Read(Join(blocked,"config"))=="preserved" && before.scale==7 && unchanged=="unchanged",
         "A legacy file used as a directory must error without changing settings or files");
}
}

int main(int argc,char** argv)
{
    try
    {
        Need(argc==2,"Expected writable fixture directory");
        const auto marker=Platform::SettingsPath(Join(argv[1],"marker.ini"),false);
        auto separator=marker.find_last_of("/\\");Need(separator!=std::string::npos,"Fixture directory did not resolve absolutely");
        Fixture fixture(marker.substr(0,separator));
        Paths(fixture.path);Legacy(fixture.path);
        std::cout<<"Settings path selection, private directories, Unicode paths and failure-preserving legacy import passed\n";
        return 0;
    }
    catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
