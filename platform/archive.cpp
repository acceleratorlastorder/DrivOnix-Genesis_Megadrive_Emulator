#include "archive.hpp"
#include "process.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace
{
constexpr std::size_t MaxRom = 0x800000 + 512;
constexpr std::size_t MaxListing = 4 * 1024 * 1024;
std::string Extension(const std::string& path)
{
    const auto at=path.find_last_of('.');
    if(at==std::string::npos) return {};
    auto extension=path.substr(at);
    std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return char(std::tolower(c));});
    return extension;
}
std::string CanonicalName(std::string name)
{
    std::replace(name.begin(),name.end(),'\\','/');return name;
}
bool Rom(const std::string& path)
{
    const auto extension=Extension(path);
    return extension==".bin"||extension==".md"||extension==".gen"||extension==".smd";
}
bool SafeName(const std::string& name)
{
    if(name.empty()||name.front()=='/'||name.front()=='\\'||name.front()=='@'||name.find(':')!=std::string::npos) return false;
    std::string part;
    for(unsigned char value:name)
    {
        if(value<0x20||value==0x7f) return false;
        if(value=='/'||value=='\\')
        {
            if(part=="..") return false;
            part.clear();
        }
        else part.push_back(char(value));
    }
    return part!="..";
}
std::size_t Size(const std::string& text)
{
    if(text.empty()) throw std::runtime_error("Archive member has no size");
    std::size_t result=0;
    for(char value:text)
    {
        if(value<'0'||value>'9'||result>(MaxRom-std::size_t(value-'0'))/10)
            throw std::runtime_error("Archive cartridge exceeds the 8 MiB image limit");
        result=result*10+std::size_t(value-'0');
    }
    return result;
}
std::string SevenZip()
{
#ifdef _WIN32
    for(const auto& name:{"7zz.exe","7z.exe","7za.exe"})
    {
        const auto found=Process::FindExecutable(name);if(!found.empty()) return found;
    }
    for(const auto* variable:{L"ProgramW6432",L"ProgramFiles",L"ProgramFiles(x86)"})
    {
        const DWORD size=GetEnvironmentVariableW(variable,nullptr,0);if(!size)continue;
        std::wstring directory(size,L'\0');
        const DWORD count=GetEnvironmentVariableW(variable,&directory[0],size);if(!count||count>=size)continue;
        directory.resize(count);const auto path=directory+L"\\7-Zip\\7z.exe";
        const DWORD attributes=GetFileAttributesW(path.c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&FILE_ATTRIBUTE_DIRECTORY))continue;
        const int bytes=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,path.data(),int(path.size()),nullptr,0,nullptr,nullptr);
        if(!bytes)continue;
        std::string result(bytes,'\0');WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,path.data(),int(path.size()),&result[0],bytes,nullptr,nullptr);
        return result;
    }
#else
    for(const auto& name:{"7zz","7z","7za"})
    {
        const auto found=Process::FindExecutable(name);if(!found.empty()) return found;
    }
#endif
    throw std::runtime_error("ZIP/7z games require installed 7-Zip (7zz, 7z or 7za). Install 7-Zip and retry; no decompressor is bundled with DrivOnix.");
}
Process::Result Run(const std::vector<std::string>& arguments,std::size_t limit)
{
    auto result=Process::Run(arguments,limit,30000);
    if(result.exitCode!=0)
    {
        auto error=result.error;
        if(error.size()>1000)error.resize(1000);
        throw std::runtime_error("7-Zip could not read the archive (corrupt, encrypted or unsupported data; exit "+
                                 std::to_string(result.exitCode)+"): "+error);
    }
    return result;
}
}

bool Archive::IsArchive(const std::string& path)
{
    const auto extension=Extension(path);return extension==".zip"||extension==".7z";
}

namespace
{
struct ListedEntry
{
    Archive::Entry entry;
    std::string rawName;
};
std::vector<ListedEntry> ListEntries(const std::string& path)
{
    if(!Archive::IsArchive(path))throw std::runtime_error("Unsupported game archive extension: "+path);
    const auto result=Run({SevenZip(),"l","-slt","-ba","-sccUTF-8","-p-","--",path},MaxListing);
    const std::string listing(result.output.begin(),result.output.end());
    if(listing.find('\0')!=std::string::npos)throw std::runtime_error("Invalid NUL in archive listing");
    std::vector<ListedEntry> entries;std::set<std::string> names;std::map<std::string,std::string> fields;
    auto record=[&]()
    {
        if(fields.empty())return;
        const auto pathField=fields.find("Path");
        if(pathField==fields.end()||!SafeName(pathField->second)) throw std::runtime_error("Archive contains an unsafe or malformed member name");
        const auto name=CanonicalName(pathField->second);
        if(!names.insert(name).second)throw std::runtime_error("Archive contains duplicate member names: "+name);
        if(fields["Encrypted"]=="+")throw std::runtime_error("Encrypted ZIP/7z games are not supported: "+name);
        const auto attributes=fields["Attributes"];
        const bool directory=fields["Folder"]=="+"||(!attributes.empty()&&attributes.front()=='D')||name.back()=='/'||name.back()=='\\';
        const bool link=fields.count("Symbolic Link")||fields.count("Hard Link")||attributes.find("lrwx")!=std::string::npos;
        if(link)throw std::runtime_error("Archive links are not supported: "+name);
        if(!directory&&Rom(name))
        {
            const auto size=Size(fields["Size"]);
            if(size<0x200)throw std::runtime_error("Archive cartridge does not contain a complete header: "+name);
            entries.push_back({{name,size},pathField->second});
        }
        fields.clear();
    };
    std::istringstream lines(listing);std::string line;
    while(std::getline(lines,line))
    {
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(line.empty()){record();continue;}
        const auto separator=line.find(" = ");
        if(separator==std::string::npos||separator==0)throw std::runtime_error("Malformed archive member listing");
        const auto key=line.substr(0,separator);
        if(!fields.emplace(key,line.substr(separator+3)).second)throw std::runtime_error("Duplicate archive member metadata");
    }
    record();
    if(entries.empty())throw std::runtime_error("Archive contains no supported Mega Drive cartridge (.bin, .md, .gen or .smd)");
    std::sort(entries.begin(),entries.end(),[](const ListedEntry& a,const ListedEntry& b){return a.entry.name<b.entry.name;});
    return entries;
}
}

std::vector<Archive::Entry> Archive::List(const std::string& path)
{
    std::vector<Entry> result;
    for(const auto& item:ListEntries(path))result.push_back(item.entry);
    return result;
}

std::vector<unsigned char> Archive::Read(const std::string& path,const std::string& member)
{
    const auto entries=ListEntries(path);std::string selected=CanonicalName(member);
    if(selected.empty())
    {
        if(entries.size()!=1)throw std::runtime_error("Archive contains multiple games; select an exact archive member");
        selected=entries.front().entry.name;
    }
    const auto entry=std::find_if(entries.begin(),entries.end(),[&](const ListedEntry& item){return item.entry.name==selected;});
    if(entry==entries.end())throw std::runtime_error("Selected cartridge is not present in the archive: "+selected);
    auto result=Run({SevenZip(),"x","-so","-bd","-bb0","-spd","-ssc","-sccUTF-8","-p-","--",path,entry->rawName},entry->entry.size);
    if(result.output.size()!=entry->entry.size)throw std::runtime_error("Archive cartridge size changed or extraction was incomplete: "+selected);
    return std::move(result.output);
}

std::string Archive::SaveIdentity(const std::string& path,const std::string& member)
{
    if(!IsArchive(path)||!SafeName(member)||!Rom(member))throw std::invalid_argument("Invalid archive save identity");
    // FNV-1a uses defined unsigned arithmetic and remains stable across operating
    // systems, releases and std::hash implementations. Never use member paths as
    // output filenames: archive separators and wildcard characters stay data.
    std::uint64_t hash=UINT64_C(14695981039346656037);
    for(unsigned char value:CanonicalName(member)){hash^=value;hash*=UINT64_C(1099511628211);}
    std::ostringstream suffix;suffix<<".member-"<<std::hex<<std::setfill('0')<<std::setw(16)<<hash;
    return path+suffix.str();
}
