#ifndef DRIVONIX_STORAGE_HPP
#define DRIVONIX_STORAGE_HPP
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "process.hpp"
namespace Storage {
struct File {
    std::string path,identity,stamp;
    uint64_t size=0;
    int64_t modified=0;
    bool directory=false,link=false;
};
std::string Join(const std::string&,const std::string&);
std::string Absolute(const std::string&);
std::string Parent(const std::string&);
std::string Filename(const std::string&);
std::string DataDirectory(bool portable=false);
void EnsureDirectory(const std::string&);
File Stat(const std::string&);
std::vector<File> List(const std::string& directory);
void Walk(const std::string& root,bool recursive,const std::function<void(const File&)>&,
          const Process::CancelToken& cancel={});
std::vector<unsigned char> ReadBytes(const std::string&,uint64_t limit,
                                   const Process::CancelToken& cancel={});
std::vector<unsigned char> ReadPrefix(const std::string&,std::size_t limit);
void AtomicWrite(const std::string&,const std::vector<unsigned char>&);
void CopyFileAtomic(const std::string& source,const std::string& destination,uint64_t limit=32*1024*1024);
bool RemoveFile(const std::string&);
}
#endif
