#include "platform.hpp"
#include "settings.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#else
#include <gtk/gtk.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace
{
std::string ExecutableDirectory()
{
    std::vector<char> path(256);
    for (;;)
    {
#ifdef _WIN32
        const DWORD length = GetModuleFileNameA(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0)
            throw std::runtime_error("Cannot find the executable directory");
        if (length < path.size())
#else
        const ssize_t length = readlink("/proc/self/exe", path.data(), path.size());
        if (length < 0)
            throw std::runtime_error("Cannot find the executable directory");
        if (static_cast<std::size_t>(length) < path.size())
#endif
        {
            const std::string executable(path.data(), length);
            return executable.substr(0, executable.find_last_of("/\\"));
        }
        path.resize(path.size() * 2);
    }
}

void CheckPath(const std::string& path)
{
    if(path.empty() || path.find('\0') != std::string::npos)
        throw std::runtime_error("Invalid settings path; use --config PATH or --portable");
}
bool Separator(char c)
{
#ifdef _WIN32
    return c=='/' || c=='\\';
#else
    return c=='/';
#endif
}
std::string Join(const std::string& directory,const std::string& child)
{
    CheckPath(directory);
    return directory + (Separator(directory.back()) ? "":"/") + child;
}
std::string Parent(const std::string& path)
{
    std::size_t last=path.size();
    while(last && !Separator(path[last-1])) --last;
    if(!last) return {};
    if(last==1 || (last>1 && path[last-2]==':')) return path.substr(0,last);
    return path.substr(0,last-1);
}
#ifdef _WIN32
std::wstring Wide(const std::string& text)
{
    CheckPath(text);
    if(text.size()>std::size_t(std::numeric_limits<int>::max())) throw std::runtime_error("Settings path is too long");
    const int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),nullptr,0);
    if(!length) throw std::runtime_error("Settings path is not valid UTF-8: "+text);
    std::wstring result(std::size_t(length),L'\0');
    if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),&result[0],length))
        throw std::runtime_error("Cannot decode settings path: "+text);
    return result;
}
std::string Utf8(const std::wstring& text)
{
    if(text.empty()) return {};
    const int length=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),int(text.size()),nullptr,0,nullptr,nullptr);
    if(!length) throw std::runtime_error("Cannot encode settings path as UTF-8");
    std::string result(std::size_t(length),'\0');
    if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),int(text.size()),&result[0],length,nullptr,nullptr))
        throw std::runtime_error("Cannot encode settings path as UTF-8");
    return result;
}
bool Absolute(const std::string& path)
{
    return (path.size()>=3 && ((path[0]>='A' && path[0]<='Z') || (path[0]>='a' && path[0]<='z'))
        && path[1]==':' && Separator(path[2]))
        || (path.size()>=3 && Separator(path[0]) && Separator(path[1]));
}
std::string FullPath(const std::string& path)
{
    const auto wide=Wide(path);
    std::vector<wchar_t> buffer(256);
    for(;;)
    {
        const DWORD length=GetFullPathNameW(wide.c_str(),DWORD(buffer.size()),buffer.data(),nullptr);
        if(!length) throw std::runtime_error("Cannot resolve settings path: "+path);
        if(length<buffer.size()) return Utf8(std::wstring(buffer.data(),length));
        buffer.resize(std::size_t(length)+1);
    }
}
std::string SettingsExecutableDirectory()
{
    std::vector<wchar_t> path(256);
    for(;;)
    {
        const DWORD length=GetModuleFileNameW(nullptr,path.data(),DWORD(path.size()));
        if(!length) throw std::runtime_error("Cannot find the settings executable directory");
        if(length<path.size()) return Parent(Utf8(std::wstring(path.data(),length)));
        path.resize(path.size()*2);
    }
}
std::string Environment(const wchar_t* name)
{
    std::vector<wchar_t> value(256);
    for(;;)
    {
        const DWORD length=GetEnvironmentVariableW(name,value.data(),DWORD(value.size()));
        if(!length) return {};
        if(length<value.size()) return Utf8(std::wstring(value.data(),length));
        value.resize(std::size_t(length)+1);
    }
}
void EnsureDirectory(const std::string& directory)
{
    const auto wide=Wide(directory);
    const DWORD attributes=GetFileAttributesW(wide.c_str());
    if(attributes!=INVALID_FILE_ATTRIBUTES)
    {
        if(!(attributes&FILE_ATTRIBUTE_DIRECTORY)) throw std::runtime_error("Settings parent is not a directory: "+directory);
        return;
    }
    const DWORD error=GetLastError();
    if(error!=ERROR_FILE_NOT_FOUND && error!=ERROR_PATH_NOT_FOUND)
        throw std::runtime_error("Cannot inspect settings directory: "+directory);
    const auto parent=Parent(directory);
    if(parent.empty() || parent==directory) throw std::runtime_error("Cannot locate settings directory root: "+directory);
    EnsureDirectory(parent);
    if(!CreateDirectoryW(wide.c_str(),nullptr) && GetLastError()!=ERROR_ALREADY_EXISTS)
        throw std::runtime_error("Cannot create settings directory: "+directory);
    const DWORD created=GetFileAttributesW(wide.c_str());
    if(created==INVALID_FILE_ATTRIBUTES || !(created&FILE_ATTRIBUTE_DIRECTORY))
        throw std::runtime_error("Settings parent is not a directory: "+directory);
}
bool ReadLegacy(const std::string& path,std::string& bytes)
{
    const auto wide=Wide(path);
    HANDLE file=CreateFileW(wide.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                            nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
    if(file==INVALID_HANDLE_VALUE)
    {
        const DWORD error=GetLastError();
        if(error==ERROR_FILE_NOT_FOUND) return false;
        if(error==ERROR_PATH_NOT_FOUND)
        {
            // Windows uses PATH_NOT_FOUND for a regular file used as a parent
            // as well as for a genuinely missing directory. Do not silently
            // treat that existing obstruction as a new/missing configuration.
            auto parent=Parent(path);
            while(!parent.empty())
            {
                const DWORD attributes=GetFileAttributesW(Wide(parent).c_str());
                if(attributes!=INVALID_FILE_ATTRIBUTES)
                {
                    if(!(attributes&FILE_ATTRIBUTE_DIRECTORY)) throw std::runtime_error("Legacy settings parent is not a directory: "+parent);
                    return false;
                }
                const DWORD parentError=GetLastError();
                if(parentError!=ERROR_FILE_NOT_FOUND && parentError!=ERROR_PATH_NOT_FOUND)
                    throw std::runtime_error("Cannot inspect legacy settings parent: "+parent);
                const auto next=Parent(parent);if(next==parent) break;parent=next;
            }
            return false;
        }
        throw std::runtime_error("Cannot open legacy settings: "+path);
    }
    struct Close { HANDLE file; ~Close() {CloseHandle(file);} } close{file};
    char buffer[4096];DWORD count=0;
    do
    {
        if(!ReadFile(file,buffer,sizeof(buffer),&count,nullptr)) throw std::runtime_error("Cannot read legacy settings: "+path);
        bytes.append(buffer,count);
        if(bytes.size()>1024*1024) throw std::runtime_error("Legacy settings file exceeds 1 MiB: "+path);
    } while(count);
    return true;
}
#else
bool Absolute(const std::string& path) { return !path.empty() && path[0]=='/'; }
std::string CurrentDirectory()
{
    std::vector<char> buffer(256);
    while(!getcwd(buffer.data(),buffer.size()))
    {
        if(errno!=ERANGE) throw std::runtime_error("Cannot resolve settings working directory");
        buffer.resize(buffer.size()*2);
    }
    return buffer.data();
}
std::string FullPath(const std::string& path)
{
    CheckPath(path);
    if(path==".") return CurrentDirectory();
    return Absolute(path) ? path:Join(CurrentDirectory(),path);
}
std::string SettingsExecutableDirectory() { return ExecutableDirectory(); }
void EnsureDirectory(const std::string& directory)
{
    struct stat info={};
    if(stat(directory.c_str(),&info)==0)
    {
        if(!S_ISDIR(info.st_mode)) throw std::runtime_error("Settings parent is not a directory: "+directory);
        return;
    }
    if(errno!=ENOENT) throw std::runtime_error("Cannot inspect settings directory: "+directory);
    const auto parent=Parent(directory);
    if(parent.empty() || parent==directory) throw std::runtime_error("Cannot locate settings directory root: "+directory);
    EnsureDirectory(parent);
    if(mkdir(directory.c_str(),0700)!=0 && errno!=EEXIST) throw std::runtime_error("Cannot create settings directory: "+directory);
    if(stat(directory.c_str(),&info)!=0 || !S_ISDIR(info.st_mode)) throw std::runtime_error("Settings parent is not a directory: "+directory);
}
bool ReadLegacy(const std::string& path,std::string& bytes)
{
    const int file=open(path.c_str(),O_RDONLY|O_NONBLOCK);
    if(file<0)
    {
        if(errno==ENOENT) return false;
        throw std::runtime_error("Cannot open legacy settings: "+path);
    }
    struct Close { int file; ~Close() {close(file);} } closeFile{file};
    struct stat info={};
    if(fstat(file,&info)!=0 || !S_ISREG(info.st_mode)) throw std::runtime_error("Legacy settings is not a regular file: "+path);
    char buffer[4096];
    for(;;)
    {
        const auto count=read(file,buffer,sizeof(buffer));
        if(count<0) { if(errno==EINTR) continue;throw std::runtime_error("Cannot read legacy settings: "+path); }
        if(!count) break;
        bytes.append(buffer,std::size_t(count));
        if(bytes.size()>1024*1024) throw std::runtime_error("Legacy settings file exceeds 1 MiB: "+path);
    }
    return true;
}
#endif
std::string Trim(const std::string& text)
{
    const auto first=text.find_first_not_of(" \t\r\n");
    if(first==std::string::npos) return {};
    return text.substr(first,text.find_last_not_of(" \t\r\n")-first+1);
}
unsigned LegacyScreen(const std::string& text,const std::string& path)
{
    bool block=false;std::size_t first=0,line=0;
    while(first<text.size())
    {
        ++line;
        auto last=text.find('\n',first);if(last==std::string::npos) last=text.size();
        std::string clean;
        for(std::size_t i=first;i<last;)
        {
            if(block) { if(i+1<last && text[i]=='*' && text[i+1]=='/') {block=false;i+=2;} else ++i; }
            else if(i+1<last && text[i]=='/' && text[i+1]=='*') {block=true;i+=2;}
            else if(i+1<last && text[i]=='/' && text[i+1]=='/') break;
            else clean+=text[i++];
        }
        if(line==1 && clean.compare(0,3,"\xef\xbb\xbf")==0) clean.erase(0,3);
        const auto equal=clean.find('=');
        if(equal!=std::string::npos && Trim(clean.substr(0,equal))=="ScreenSize")
        {
            auto value=clean.substr(equal+1);const auto comment=value.find_first_of(";#");
            if(comment!=std::string::npos) value.resize(comment);
            value=Trim(value);
            if(value.size()!=1 || value[0]<'1' || value[0]>'3')
                throw std::runtime_error(path+": legacy settings line "+std::to_string(line)+" (ScreenSize): expected 1, 2 or 3");
            return unsigned(value[0]-'0');
        }
        first=last==text.size() ? last:last+1;
    }
    throw std::runtime_error(path+": legacy settings has no ScreenSize assignment");
}
}

std::string Platform::SelectRom()
{
#ifdef _WIN32
    char path[32768] = {};
    OPENFILENAMEA dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter = "Genesis/Megadrive ROMs\0*.bin;*.md;*.gen;*.smd\0All files\0*.*\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = sizeof(path);
    dialog.lpstrTitle = "Select a ROM";
    dialog.Flags = OFN_DONTADDTORECENT | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameA(&dialog))
        return path;
    if (CommDlgExtendedError() != 0)
        throw std::runtime_error("Cannot open the ROM file dialog");
    return {};
#else
    if (!gtk_init_check(nullptr, nullptr))
        throw std::runtime_error("Cannot open the ROM picker. Run DrivOnix on a desktop or pass a ROM path.");
    GtkWidget* dialog = gtk_file_chooser_dialog_new("Select a ROM", nullptr,
        GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open", GTK_RESPONSE_ACCEPT, nullptr);
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Genesis/Megadrive ROMs");
    for (const char* pattern : {"*.[bB][iI][nN]", "*.[mM][dD]", "*.[gG][eE][nN]", "*.[sS][mM][dD]"})
        gtk_file_filter_add_pattern(filter, pattern);
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
    GtkFileFilter* allFiles = gtk_file_filter_new();
    gtk_file_filter_set_name(allFiles, "All files");
    gtk_file_filter_add_pattern(allFiles, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), allFiles);

    std::string selection;
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
    {
        char* path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (path)
        {
            selection = path;
            g_free(path);
        }
    }
    gtk_widget_destroy(dialog);
    gdk_display_flush(gdk_display_get_default());
    return selection;
#endif
}

std::string Platform::DataFile(const std::string& relativePath)
{
    const std::string directory = ExecutableDirectory();
    // Preserve local configuration overrides and support both build and install trees.
    const std::vector<std::string> candidates = {
        relativePath,
        directory + "/" + relativePath,
        directory + "/../share/drivonix/" + relativePath,
#ifdef DRIVONIX_INSTALL_DATA_DIR
        std::string(DRIVONIX_INSTALL_DATA_DIR) + "/" + relativePath,
#endif
    };
    for (const auto& candidate : candidates)
        if (std::ifstream(candidate, std::ios::binary).good())
            return candidate;
    throw std::runtime_error("Cannot find " + relativePath + ". Keep the bios and config directories with drivonix.");
}

std::string Platform::SettingsPath(const std::string& custom,bool portable)
{
    if(!custom.empty() && portable) throw std::invalid_argument("--config and --portable cannot be used together");
    if(!custom.empty()) return FullPath(custom);
    if(portable) return Join(SettingsExecutableDirectory(),"config/drivonix.ini");
#ifdef _WIN32
    std::string base=Environment(L"LOCALAPPDATA");
    if(base.empty() || !Absolute(base))
    {
        struct Folder { PWSTR path=nullptr;~Folder(){CoTaskMemFree(path);} } folder;
        if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,KF_FLAG_DEFAULT,nullptr,&folder.path)))
            throw std::runtime_error("Cannot locate settings directory; use --config PATH or --portable");
        base=Utf8(folder.path);
    }
    return Join(FullPath(base),"drivonix/drivonix.ini");
#else
    const char* xdg=std::getenv("XDG_CONFIG_HOME");
    std::string base=xdg ? xdg:"";
    if(base.empty() || !Absolute(base))
    {
        const char* home=std::getenv("HOME");
        if(!home || !Absolute(home)) throw std::runtime_error("Cannot locate settings directory; set HOME or use --config PATH or --portable");
        base=Join(home,".config");
    }
    return Join(base,"drivonix/drivonix.ini");
#endif
}

void Platform::EnsureSettingsDirectory(const std::string& filePath)
{
    const auto parent=Parent(FullPath(filePath));
    if(!parent.empty()) EnsureDirectory(parent);
}

bool Platform::ImportLegacySettings(Settings::Values& values,std::string& sourcePath)
{
    const auto executable=SettingsExecutableDirectory();
    std::vector<std::string> bases={FullPath("."),executable,Join(executable,"../share/drivonix")};
#ifdef DRIVONIX_INSTALL_DATA_DIR
    bases.push_back(DRIVONIX_INSTALL_DATA_DIR);
#endif
    for(const auto& base:bases) for(const char* relative:{"config/config.txt","Config/config.txt"})
    {
        const auto path=Join(base,relative);std::string bytes;
        if(!ReadLegacy(path,bytes)) continue;
        const auto screen=LegacyScreen(bytes,path);
        auto imported=values;imported.scale=screen==2 ? 2:1;imported.fullscreen=screen==3;
        Settings::Validate(imported);
        values=imported;sourcePath=path;return true;
    }
    return false;
}

void Platform::CheckDisplay()
{
#ifndef _WIN32
    const char* display = std::getenv("DISPLAY");
    if (!display || !*display)
        throw std::runtime_error("DrivOnix needs an X11 display (or XWayland) to show the emulator window.");
#endif
}
