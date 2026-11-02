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
uint64_t Size(const std::string& text)
{
    if(text.empty())throw std::runtime_error("Archive member has no size");
    uint64_t result=0;
    for(char value:text){if(value<'0'||value>'9'||result>(UINT64_MAX-uint64_t(value-'0'))/10)throw std::runtime_error("Invalid archive member size");result=result*10+uint64_t(value-'0');}
    return result;
}
std::string SevenZip()
{
#ifdef _WIN32
    for(const auto& name:{"7zz.exe","7z.exe","7za.exe"})
    {
        const auto found=Process::FindProgram(name);if(!found.empty()) return found;
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
        const auto found=Process::FindProgram(name);if(!found.empty()) return found;
    }
#endif
    throw std::runtime_error("ZIP/7z games require installed 7-Zip (7zz, 7z or 7za). Install 7-Zip and retry; no decompressor is bundled with DrivOnix.");
}
Process::Result Run(const std::vector<std::string>& arguments,std::size_t limit,const Process::CancelToken& cancel)
{
    auto result=Process::Run(arguments,limit,30000,cancel);
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
struct ListedEntry {Archive::MemberInfo info;std::string rawName;};
std::vector<ListedEntry> ListEntries(const std::string& path,const Process::CancelToken& cancel)
{
    if(!Archive::IsArchive(path))throw std::runtime_error("Unsupported game archive extension: "+path);
    const auto result=Run({SevenZip(),"l","-slt","-ba","-sccUTF-8","-p-","--",path},MaxListing,cancel);
    const std::string listing(result.output.begin(),result.output.end());
    if(listing.find('\0')!=std::string::npos)throw std::runtime_error("Invalid NUL in archive listing");
    std::vector<ListedEntry> entries;std::map<std::string,std::string> fields;
    auto record=[&]() {
        if(fields.empty())return;
        const auto found=fields.find("Path");
        if(found==fields.end())throw std::runtime_error("Archive member listing has no path");
        const auto name=CanonicalName(found->second);
        if(!Rom(name)){fields.clear();return;}
        ListedEntry item;item.rawName=found->second;item.info.name=name;item.info.available=true;item.info.status="available";
        auto fail=[&](const std::string& status,const std::string& reason){item.info.available=false;item.info.status=status;item.info.diagnostic=reason+": "+name;};
        if(!SafeName(found->second))fail("unsafe","Archive contains an unsafe member name");
        else if(fields["Encrypted"]=="+")fail("encrypted","Encrypted ZIP/7z games are not supported");
        else if(fields.count("Symbolic Link")||fields.count("Hard Link")||fields["Attributes"].find("lrwx")!=std::string::npos)fail("link","Archive links are not supported");
        else if(fields["Folder"]=="+"||(!fields["Attributes"].empty()&&fields["Attributes"].front()=='D'))fail("directory","Cartridge member is a directory");
        try {
            item.info.size=Size(fields["Size"]);
            if(item.info.available&&item.info.size<0x200)fail("undersized","Archive cartridge does not contain a complete header");
            else if(item.info.available&&item.info.size>MaxRom-(Extension(name)==".smd" ? 0:512))fail("oversized","Archive cartridge exceeds the 8 MiB image limit");
        }catch(const std::runtime_error& error){fail("invalid-size",error.what());}
        const auto crc=fields["CRC"];
        if(crc.size()==8){uint32_t value=0;bool valid=true;for(unsigned char c:crc){unsigned digit=c>='0'&&c<='9'?c-'0':c>='A'&&c<='F'?c-'A'+10:c>='a'&&c<='f'?c-'a'+10:16;if(digit>15){valid=false;break;}value=(value<<4)|digit;}if(valid){item.info.hasCrc=true;item.info.crc32=value;}}
        entries.push_back(std::move(item));fields.clear();
    };
    std::istringstream lines(listing);std::string line;
    while(std::getline(lines,line)) {
        Process::CheckCancelled(cancel);
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(line.empty()){record();continue;}
        const auto at=line.find(" = ");if(at==std::string::npos||at==0)throw std::runtime_error("Malformed archive member listing");
        if(!fields.emplace(line.substr(0,at),line.substr(at+3)).second)throw std::runtime_error("Duplicate archive member metadata");
    }
    record();std::map<std::string,unsigned> counts;for(const auto& item:entries)++counts[item.info.name];
    for(auto& item:entries)if(counts[item.info.name]>1){item.info.available=false;item.info.status="duplicate";item.info.diagnostic="Archive contains duplicate member names: "+item.info.name;}
    std::sort(entries.begin(),entries.end(),[](const ListedEntry& a,const ListedEntry& b){return a.info.name<b.info.name;});
    return entries;
}
}
Archive::Inspection Archive::Inspect(const std::string& path,const Process::CancelToken& cancel)
{
    Inspection result;for(const auto& item:ListEntries(path,cancel))result.members.push_back(item.info);return result;
}
std::vector<Archive::Entry> Archive::List(const std::string& path,const Process::CancelToken& cancel)
{
    const auto inspected=Inspect(path,cancel);std::vector<Entry> result;
    for(const auto& item:inspected.members)if(item.available)result.push_back({item.name,std::size_t(item.size)});
    if(result.empty()){if(!inspected.members.empty())throw std::runtime_error(inspected.members.front().diagnostic);throw std::runtime_error("Archive contains no supported Mega Drive cartridge (.bin, .md, .gen or .smd)");}
    return result;
}
std::vector<unsigned char> Archive::Read(const std::string& path,const std::string& member,const Process::CancelToken& cancel)
{
    const auto entries=ListEntries(path,cancel);std::string selected=CanonicalName(member);
    if(selected.empty()){
        for(const auto& item:entries)if(item.info.available){if(!selected.empty())throw std::runtime_error("Archive contains multiple games; select an exact archive member");selected=item.info.name;}
    }
    const auto item=std::find_if(entries.begin(),entries.end(),[&](const ListedEntry& value){return value.info.name==selected;});
    if(item==entries.end())throw std::runtime_error("Selected cartridge is not present in the archive: "+selected);
    if(!item->info.available)throw std::runtime_error(item->info.diagnostic);
    auto result=Run({SevenZip(),"x","-so","-bd","-bb0","-spd","-ssc","-sccUTF-8","-p-","--",path,item->rawName},std::size_t(item->info.size),cancel);
    if(result.output.size()!=item->info.size)throw std::runtime_error("Archive cartridge size changed or extraction was incomplete: "+selected);
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
