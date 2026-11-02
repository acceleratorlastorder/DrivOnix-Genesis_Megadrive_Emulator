#ifndef DRIVONIX_METADATA_HTTP_HPP
#define DRIVONIX_METADATA_HTTP_HPP

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace Metadata {
using CancelCheck = std::function<bool()>;
struct HttpRequest {
    std::string url;
    std::string etag;
    std::size_t limit = 16 * 1024 * 1024;
    unsigned timeoutSeconds = 20;
};
struct HttpResponse {
    unsigned status = 0;
    std::string etag, contentType;
    std::vector<unsigned char> bytes;
};
using Fetch = std::function<HttpResponse(const HttpRequest&, const CancelCheck&)>;
bool AllowedUrl(const std::string& url);
HttpResponse Download(const HttpRequest& request, const CancelCheck& cancel = {});
}
#endif
