#ifndef DRIVONIX_PROCESS_HPP
#define DRIVONIX_PROCESS_HPP

#include <cstddef>
#include <atomic>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace Process
{
using CancelToken=std::shared_ptr<std::atomic<bool>>;
class Cancelled:public std::runtime_error {public:Cancelled():std::runtime_error("Operation cancelled"){};};
inline void CheckCancelled(const CancelToken& token){if(token&&token->load())throw Cancelled();}
struct Result
{
    int exitCode = -1;
    std::vector<unsigned char> output;
    std::string error;
};
// Search the explicitly configured PATH, never an implicit working-directory
// executable. An empty result means that no executable was found.
std::string FindProgram(const std::string& name);
// Pass arguments directly to the native process API, without a shell. Child
// stdin is closed, stdout is bounded, and timeout/overflow terminate the child.
Result Run(const std::vector<std::string>& arguments, std::size_t outputLimit,
           unsigned timeoutMilliseconds = 30000,const CancelToken& cancel={});
}

#endif
