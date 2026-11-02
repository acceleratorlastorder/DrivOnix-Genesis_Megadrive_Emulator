#ifndef DRIVONIX_ARCHIVE_HPP
#define DRIVONIX_ARCHIVE_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "process.hpp"

namespace Archive
{
struct Entry
{
    std::string name;
    std::size_t size;
};
struct MemberInfo {
    std::string name,status,diagnostic;
    uint64_t size=0;
    uint32_t crc32=0;
    bool hasCrc=false,available=false;
};
struct Inspection {std::vector<MemberInfo> members;};
Inspection Inspect(const std::string& path,const Process::CancelToken& cancel={});
bool IsArchive(const std::string& path);
// List supported cartridge members, preserving UTF-8 names and case while
// normalizing path separators to '/' on both platforms. Encrypted,
// malformed, duplicate and unsafe entries are rejected, never extracted to disk.
std::vector<Entry> List(const std::string& path,const Process::CancelToken& cancel={});
// An empty member selects the only cartridge in the archive. Ambiguous archives
// require an explicit member from List; extraction is bounded to 8 MiB + header.
std::vector<unsigned char> Read(const std::string& path,const std::string& member = "",const Process::CancelToken& cancel={});
// Each archive member gets a deterministic sidecar save identity without putting
// member paths in the host filesystem or colliding with another selected game.
std::string SaveIdentity(const std::string& path,const std::string& member);
}

#endif
