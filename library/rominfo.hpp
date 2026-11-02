#ifndef DRIVONIX_ROMINFO_HPP
#define DRIVONIX_ROMINFO_HPP
#include <cstdint>
#include <string>
#include <vector>
#include "../platform/process.hpp"
namespace RomInfo {
struct Header {
    std::string title,domesticTitle,product,regions;
    unsigned char region='U';
    bool sega=false;
};
struct Prepared {
    std::vector<unsigned char> bytes;
    unsigned char region='U';
    std::string title,saveIdentity,sha256;
    uint64_t size=0;
    uint32_t crc32=0;
    Header header;
};
Header ProbeHeader(const std::vector<unsigned char>& normalizedBytes);
std::string Sha256(const std::vector<unsigned char>& bytes);
uint32_t Crc32(const std::vector<unsigned char>& bytes);
Prepared Load(const std::string& path,const std::string& member="",
              const Process::CancelToken& cancel={});
}
#endif
