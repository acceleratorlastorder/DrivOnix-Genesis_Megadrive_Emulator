#ifndef DRIVONIX_PLATFORM_HPP
#define DRIVONIX_PLATFORM_HPP

#include <string>

namespace Platform
{
// An empty selection means the user cancelled the dialog.
std::string SelectRom();
std::string DataFile(const std::string& relativePath);
void CheckDisplay();
}

#endif
