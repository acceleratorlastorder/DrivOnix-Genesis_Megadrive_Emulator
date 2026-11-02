#ifndef DRIVONIX_LIBRARY_HPP
#define DRIVONIX_LIBRARY_HPP
#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <vector>
#include "rominfo.hpp"
namespace Library {
using Id=int64_t;
struct Root {std::string path;bool recursive=true;std::string id;};
struct Locator {std::string path,member;};
struct Metadata {
    std::string catalogId,title,developer,publisher,genre,description,artworkPath,region;
    int year=0,players=0;
};
struct CatalogRecord {uint32_t crc32=0;uint64_t size=0;Metadata metadata;};
struct Copy {
    Id id=0;
    Locator locator;
    std::string saveIdentity,status,error,sha256;
    uint64_t size=0;
    uint32_t crc32=0;
    bool hasCrc=false,available=false,duplicateCandidate=false;
    uint64_t normalizedSize=0;
    uint32_t normalizedCrc32=0;
    bool hasNormalizedCrc=false;
};
struct Variant {
    Id id=0,preferredCopy=0;
    std::string title,region,tags;
    std::vector<Copy> copies;
};
struct Game {
    Id id=0;
    Id preferredVariant=0;
    std::string title;
    Metadata metadata;
    bool favorite=false,manualOverride=false;
    uint64_t playSeconds=0;
    int64_t lastPlayed=0;
    std::vector<Variant> variants;
};
enum class Sort {Title,LastPlayed,MostPlayed};
struct Query {
    std::string search,region,genre;
    int year=0;
    bool favoritesOnly=false,recentOnly=false,includeUnavailable=true;
    Sort sort=Sort::Title;
    std::size_t offset=0,limit=50;
};
struct Progress {
    bool scanning=false,cancelled=false;
    uint64_t files=0,inspected=0,unchanged=0,errors=0;
    std::string current;
};
struct Snapshot {
    std::vector<Game> games;
    // Complete indexed facets, independent of the current query/page.
    std::vector<std::string> genres,regions;
    std::vector<int> years;
    uint64_t revision=0,totalGames=0,totalVariants=0,totalCopies=0;
    Progress progress;
    bool ready=false,busy=false;
    std::string error;
};
class Index {
public:
    explicit Index(std::string databasePath);
    ~Index();
    Index(const Index&)=delete;
    Index& operator=(const Index&)=delete;
    void Configure(std::vector<Root> roots,bool scan=true);
    void Scan(bool force=false);
    void CancelScan();
    void QueryPage(Query query);
    std::shared_ptr<const Snapshot> GetSnapshot() const;
    void SetFavorite(Id gameId,bool value);
    void SetPreferredCopy(Id variantId,Id copyId);
    void SetPreferredVariant(Id gameId,Id variantId);
    void ApplyMetadata(Id gameId,Metadata metadata,bool manual=false);
    void ApplyCatalog(std::vector<CatalogRecord> records);
    void RecordSession(Id copyId,uint64_t activeSeconds,const std::string& outcome="completed");
    void RecordLoaded(Id copyId,const RomInfo::Prepared&);
    void RecordExternal(const Locator&,const RomInfo::Prepared&,uint64_t activeSeconds=0,
                        const std::string& outcome="started");
    // Rebuild metadata only; persistent source IDs, user state and sessions stay.
    void Rebuild();
    // A completion fence for tests/headless callers; the UI polls snapshots.
    std::future<void> Barrier();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
#endif
