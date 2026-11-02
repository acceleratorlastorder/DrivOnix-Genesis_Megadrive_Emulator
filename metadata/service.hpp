#ifndef DRIVONIX_METADATA_SERVICE_HPP
#define DRIVONIX_METADATA_SERVICE_HPP

#include "catalog.hpp"
#include "http.hpp"
#include <memory>
#include <string>
#include <vector>

namespace Metadata {
struct ImageInfo { unsigned width = 0, height = 0; };
// Strict PNG envelope/dimension validation; SFML performs the final decode.
ImageInfo ValidatePng(const std::vector<unsigned char>& bytes);
struct Options {
    bool enabled = true;
    std::size_t cacheLimitBytes = 256 * 1024 * 1024;
    unsigned refreshSeconds = 24 * 60 * 60;
};
struct Event {
    enum class Type { CatalogReady, CoverReady, Error };
    Type type = Type::Error;
    std::string key, path, message;
    std::vector<unsigned char> bytes;
};
class Service {
public:
    // All disk/network work happens on the worker; constructor never scans ROMs.
    explicit Service(const std::string& cacheDirectory, const Options& options = {}, Fetch fetch = Download);
    ~Service();
    Service(const Service&) = delete;
    Service& operator=(const Service&) = delete;
    void Configure(const Options& options);
    void RefreshCatalog();
    std::shared_ptr<const Catalog> Snapshot() const;
    // Requests replace the prior viewport, canceling downloads which are no
    // longer relevant. Covers use local overrides even in offline mode.
    struct CoverRequest { std::string key, title, localPath; };
    void RequestCovers(const std::vector<CoverRequest>& visibleAndNearby);
    std::vector<Event> PollEvents();
    void ClearCache();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
#endif
