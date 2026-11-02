#ifndef DRIVONIX_METADATA_CATALOG_HPP
#define DRIVONIX_METADATA_CATALOG_HPP

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace Metadata {
struct Game {
    std::string id, title, region, developer, publisher, genre;
    unsigned year = 0, players = 0;
    std::uint64_t size = 0;
    std::uint32_t crc32 = 0;
    bool hasChecksum = false;
};

class Catalog {
public:
    // Supplement names are developer, publisher, genre, releaseyear or maxusers.
    // Parsing is pure and transactional: invalid data never mutates a snapshot.
    static Catalog Parse(const std::string& noIntro,
        const std::vector<std::pair<std::string, std::string>>& supplements = {});
    const std::vector<Game>& Games() const;
    std::vector<Game> Match(std::uint32_t crc32, std::uint64_t size) const;
    std::vector<Game> Search(const std::string& query, std::size_t limit = 100) const;
    const Game* Find(const std::string& id) const;
private:
    std::vector<Game> games_;
};

std::string CoverUrl(const std::string& canonicalTitle);
std::string CatalogUrl(const std::string& category);
const char* Attribution();
}
#endif
