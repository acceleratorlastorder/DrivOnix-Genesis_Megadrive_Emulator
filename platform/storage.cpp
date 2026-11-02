#include "storage.hpp"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace {
std::runtime_error Error(const std::string& operation,const std::string& path){return std::runtime_error(operation+": "+path);}
#ifdef _WIN32
std::wstring Wide(const std::string& text){
    if(text.find('\0')!=std::string::npos)throw Error("NUL in path",text);
    if(text.empty())return {};
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),nullptr,0);
    if(!n)throw Error("Invalid UTF-8 path",text);
    std::wstring out(n,0);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),&out[0],n);return out;
}
std::string Utf8(const std::wstring& text){
    if(text.empty())return {};
    int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),int(text.size()),nullptr,0,nullptr,nullptr);
    if(!n)throw std::runtime_error("Invalid Unicode path");
    std::string out(n,0);WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),int(text.size()),&out[0],n,nullptr,nullptr);return out;
}
std::string Env(const wchar_t* name){DWORD n=GetEnvironmentVariableW(name,nullptr,0);if(!n)return {};std::wstring out(n,0);DWORD got=GetEnvironmentVariableW(name,&out[0],n);if(!got||got>=n)return {};out.resize(got);return Utf8(out);}
uint64_t Ticks(FILETIME time){return (uint64_t(time.dwHighDateTime)<<32)|time.dwLowDateTime;}
std::FILE* Open(const std::string& path){return _wfopen(Wide(path).c_str(),L"rb");}
#else
std::FILE* Open(const std::string& path){if(path.find('\0')!=std::string::npos)throw Error("NUL in path",path);return std::fopen(path.c_str(),"rb");}
#endif
}
std::string Storage::Join(const std::string& a,const std::string& b){if(a.empty())return b;if(b.empty())return a;return a+(a.back()=='/'||a.back()=='\\' ? "":"/")+b;}
std::string Storage::Filename(const std::string& path){const auto at=path.find_last_of("/\\");return at==std::string::npos ? path:path.substr(at+1);}
std::string Storage::Parent(const std::string& path){
    auto end=path.size();while(end>1&&(path[end-1]=='/'||path[end-1]=='\\'))--end;
    auto at=path.find_last_of("/\\",end-1);if(at==std::string::npos)return ".";
    if(at==0)return path.substr(0,1);
    if(at==2&&path.size()>2&&path[1]==':')return path.substr(0,3);
    return path.substr(0,at);
}
std::string Storage::Absolute(const std::string& path){
    if(path.empty()||path.find('\0')!=std::string::npos)throw Error("Invalid empty/NUL path",path);
#ifdef _WIN32
    const auto wide=Wide(path);DWORD size=GetFullPathNameW(wide.c_str(),0,nullptr,nullptr);if(!size)throw Error("Cannot resolve path",path);
    std::wstring out(size,0);DWORD n=GetFullPathNameW(wide.c_str(),size,&out[0],nullptr);if(!n||n>=size)throw Error("Cannot resolve path",path);out.resize(n);
    auto result=Utf8(out);std::replace(result.begin(),result.end(),'\\','/');return result;
#else
    std::string text=path;
    if(text.front()!='/'){std::vector<char> cwd(4096);while(!getcwd(cwd.data(),cwd.size())){if(errno!=ERANGE)throw Error("Cannot read working directory",path);cwd.resize(cwd.size()*2);}text=Join(cwd.data(),text);}
    std::vector<std::string> parts;std::size_t start=0;
    while(start<=text.size()){auto end=text.find('/',start);auto part=text.substr(start,end==std::string::npos ? end:end-start);if(part==".."){if(!parts.empty())parts.pop_back();}else if(!part.empty()&&part!=".")parts.push_back(part);if(end==std::string::npos)break;start=end+1;}
    std::string result="/";for(const auto& part:parts)result=Join(result,part);return result;
#endif
}
std::string Storage::DataDirectory(bool portable){
    if(portable){
#ifdef _WIN32
        std::wstring executable(32768,0);DWORD n=GetModuleFileNameW(nullptr,&executable[0],DWORD(executable.size()));if(!n||n>=executable.size())throw std::runtime_error("Cannot locate portable executable");executable.resize(n);return Join(Parent(Utf8(executable)),"data");
#else
        std::vector<char> executable(4096);auto n=readlink("/proc/self/exe",executable.data(),executable.size());if(n<0||std::size_t(n)>=executable.size())throw std::runtime_error("Cannot locate portable executable");return Join(Parent(std::string(executable.data(),std::size_t(n))),"data");
#endif
    }
#ifdef _WIN32
    auto root=Env(L"LOCALAPPDATA");
    if(root.empty()){PWSTR path=nullptr;if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&path))){root=Utf8(path);CoTaskMemFree(path);}}
    if(root.empty())throw std::runtime_error("No writable user data directory; use portable mode");return Join(root,"drivonix");
#else
    const char* xdg=std::getenv("XDG_DATA_HOME");if(xdg&&xdg[0]=='/')return Join(xdg,"drivonix");
    const char* home=std::getenv("HOME");if(!home||home[0]!='/')throw std::runtime_error("No writable user data directory; use portable mode");return Join(Join(home,".local/share"),"drivonix");
#endif
}
Storage::File Storage::Stat(const std::string& path){
    File result;result.path=path;
#ifdef _WIN32
    const auto wide=Wide(path);DWORD attr=GetFileAttributesW(wide.c_str());if(attr==INVALID_FILE_ATTRIBUTES)throw Error("Cannot inspect file",path);
    result.directory=(attr&FILE_ATTRIBUTE_DIRECTORY)!=0;result.link=(attr&FILE_ATTRIBUTE_REPARSE_POINT)!=0;
    HANDLE handle=CreateFileW(wide.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(handle==INVALID_HANDLE_VALUE)throw Error("Cannot inspect file identity",path);
    BY_HANDLE_FILE_INFORMATION info={};FILE_BASIC_INFO basic={};bool ok=GetFileInformationByHandle(handle,&info)!=0;
    bool changed=GetFileInformationByHandleEx(handle,FileBasicInfo,&basic,sizeof(basic))!=0;CloseHandle(handle);if(!ok)throw Error("Cannot inspect file identity",path);
    result.size=(uint64_t(info.nFileSizeHigh)<<32)|info.nFileSizeLow;
    result.modified=int64_t(Ticks(info.ftLastWriteTime)/10000000)-INT64_C(11644473600);
    result.identity=std::to_string(info.dwVolumeSerialNumber)+":"+std::to_string((uint64_t(info.nFileIndexHigh)<<32)|info.nFileIndexLow);
    result.stamp=result.identity+":"+std::to_string(result.size)+":"+std::to_string(Ticks(info.ftLastWriteTime))+":"+std::to_string(changed ? basic.ChangeTime.QuadPart:0);
#else
    struct stat info={};if(lstat(path.c_str(),&info)!=0)throw Error("Cannot inspect file",path);
    result.directory=S_ISDIR(info.st_mode);result.link=S_ISLNK(info.st_mode);
    if(!result.directory&&!result.link&&!S_ISREG(info.st_mode))throw Error("Unsupported special file",path);
    result.size=uint64_t(info.st_size);result.modified=info.st_mtim.tv_sec;result.identity=std::to_string(info.st_dev)+":"+std::to_string(info.st_ino);
    result.stamp=result.identity+":"+std::to_string(result.size)+":"+std::to_string(info.st_mtim.tv_sec)+":"+std::to_string(info.st_mtim.tv_nsec)+":"+std::to_string(info.st_ctim.tv_sec)+":"+std::to_string(info.st_ctim.tv_nsec);
#endif
    return result;
}
void Storage::EnsureDirectory(const std::string& requested){
    const auto path=Absolute(requested);
#ifndef _WIN32
    struct stat existing={};if(stat(path.c_str(),&existing)==0&&S_ISDIR(existing.st_mode))return;
#endif
    try{if(Stat(path).directory)return;throw Error("Path is not a directory",path);}catch(const std::runtime_error&){
#ifdef _WIN32
        const DWORD attr=GetFileAttributesW(Wide(path).c_str());if(attr!=INVALID_FILE_ATTRIBUTES)throw Error("Path is not a directory",path);
#else
        struct stat status={};if(lstat(path.c_str(),&status)==0)throw Error("Path is not a directory",path);if(errno!=ENOENT)throw Error("Cannot inspect directory",path);
#endif
    }
    const auto parent=Parent(path);if(parent==path)throw Error("Cannot create directory",path);EnsureDirectory(parent);
#ifdef _WIN32
    if(!CreateDirectoryW(Wide(path).c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)throw Error("Cannot create directory",path);
#else
    if(mkdir(path.c_str(),0700)!=0&&errno!=EEXIST)throw Error("Cannot create directory",path);
#endif
    if(!Stat(path).directory)throw Error("Created path is not a directory",path);
}
std::vector<Storage::File> Storage::List(const std::string& path){
    std::vector<File> result;
#ifdef _WIN32
    WIN32_FIND_DATAW entry={};HANDLE search=FindFirstFileW(Wide(Join(path,"*")).c_str(),&entry);
    if(search==INVALID_HANDLE_VALUE){if(GetLastError()==ERROR_FILE_NOT_FOUND)return result;throw Error("Cannot list directory",path);}
    try{do{std::wstring name=entry.cFileName;if(name!=L"."&&name!=L"..")result.push_back(Stat(Join(path,Utf8(name))));}while(FindNextFileW(search,&entry));DWORD code=GetLastError();if(code!=ERROR_NO_MORE_FILES)throw Error("Cannot finish directory listing",path);}catch(...){FindClose(search);throw;}FindClose(search);
#else
    DIR* directory=opendir(path.c_str());if(!directory)throw Error("Cannot list directory",path);
    try{errno=0;while(auto* entry=readdir(directory)){const std::string name=entry->d_name;if(name!="."&&name!="..")result.push_back(Stat(Join(path,name)));errno=0;}if(errno)throw Error("Cannot finish directory listing",path);}catch(...){closedir(directory);throw;}closedir(directory);
#endif
    std::sort(result.begin(),result.end(),[](const File& a,const File& b){return a.path<b.path;});return result;
}
void Storage::Walk(const std::string& root,bool recursive,const std::function<void(const File&)>& visitor,const Process::CancelToken& cancel){
    std::vector<std::pair<std::string,unsigned>> pending={{Absolute(root),0}};
    while(!pending.empty()){Process::CheckCancelled(cancel);auto item=pending.back();pending.pop_back();const auto directory=Stat(item.first);if(!directory.directory||directory.link)throw Error("Library root is not an ordinary directory",item.first);
        for(const auto& file:List(item.first)){Process::CheckCancelled(cancel);if(file.link)continue;if(file.directory){if(recursive){if(item.second>=128)throw Error("Directory nesting limit exceeded",file.path);pending.push_back({file.path,item.second+1});}}else visitor(file);}
    }
}
std::vector<unsigned char> Storage::ReadBytes(const std::string& path,uint64_t limit,const Process::CancelToken& cancel){
    Process::CheckCancelled(cancel);const auto stamp=Stat(path);if(stamp.link||stamp.directory||stamp.size>limit)throw Error("File exceeds allowed size or is not an ordinary file",path);
    std::unique_ptr<std::FILE,int(*)(std::FILE*)> input(Open(path),std::fclose);if(!input)throw Error("Cannot read file",path);
    std::vector<unsigned char> bytes(static_cast<std::size_t>(stamp.size));std::size_t at=0;
    while(at<bytes.size()){Process::CheckCancelled(cancel);const auto count=std::min<std::size_t>(65536,bytes.size()-at);if(std::fread(bytes.data()+at,1,count,input.get())!=count)throw Error("Incomplete file read",path);at+=count;}
    if(Stat(path).stamp!=stamp.stamp)throw Error("File changed while reading",path);
    return bytes;
}
std::vector<unsigned char> Storage::ReadPrefix(const std::string& path,std::size_t limit){
    std::unique_ptr<std::FILE,int(*)(std::FILE*)> input(Open(path),std::fclose);if(!input)throw Error("Cannot read file",path);
    std::vector<unsigned char> bytes(limit);const auto count=std::fread(bytes.data(),1,limit,input.get());if(std::ferror(input.get()))throw Error("Cannot read file prefix",path);bytes.resize(count);return bytes;
}
bool Storage::RemoveFile(const std::string& path){
#ifdef _WIN32
    if(DeleteFileW(Wide(path).c_str()))return true;
    if(GetLastError()==ERROR_FILE_NOT_FOUND)return false;
#else
    if(std::remove(path.c_str())==0)return true;
    if(errno==ENOENT)return false;
#endif
    throw Error("Cannot remove file",path);
}
void Storage::AtomicWrite(const std::string& path,const std::vector<unsigned char>& bytes){
    EnsureDirectory(Parent(path));static std::atomic<uint64_t> sequence{0};
    const auto temporary=path+".tmp."+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"."+std::to_string(sequence++);
    bool complete=false;
    try{
#ifdef _WIN32
        HANDLE output=CreateFileW(Wide(temporary).c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,nullptr);if(output==INVALID_HANDLE_VALUE)throw Error("Cannot create temporary file",path);
        std::size_t offset=0;while(offset<bytes.size()){DWORD count=0;DWORD request=DWORD(std::min<std::size_t>(65536,bytes.size()-offset));if(!WriteFile(output,bytes.data()+offset,request,&count,nullptr)||count!=request)break;offset+=count;}
        const bool flushed=offset==bytes.size()&&FlushFileBuffers(output)!=0;const bool closed=CloseHandle(output)!=0;if(!flushed||!closed)throw Error("Cannot flush temporary file",path);
        if(!MoveFileExW(Wide(temporary).c_str(),Wide(path).c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw Error("Cannot replace file",path);
#else
        int output=open(temporary.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600);if(output<0)throw Error("Cannot create temporary file",path);
        std::size_t offset=0;while(offset<bytes.size()){auto count=write(output,bytes.data()+offset,bytes.size()-offset);if(count<0&&errno==EINTR)continue;if(count<=0)break;offset+=std::size_t(count);}
        const bool flushed=offset==bytes.size()&&fsync(output)==0;const bool closed=close(output)==0;if(!flushed||!closed)throw Error("Cannot flush temporary file",path);
        if(std::rename(temporary.c_str(),path.c_str())!=0)throw Error("Cannot replace file",path);
#endif
        complete=true;
#ifndef _WIN32
        int directory=open(Parent(path).c_str(),O_RDONLY|O_DIRECTORY);if(directory<0)throw Error("Cannot synchronize directory",path);const bool synced=fsync(directory)==0;close(directory);if(!synced)throw Error("Cannot synchronize directory",path);
#endif
    }catch(...){if(!complete){try{RemoveFile(temporary);}catch(...){}}throw;}
}
void Storage::CopyFileAtomic(const std::string& source,const std::string& destination,uint64_t limit){AtomicWrite(destination,ReadBytes(source,limit));}
