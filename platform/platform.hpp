#ifndef DRIVONIX_PLATFORM_HPP
#define DRIVONIX_PLATFORM_HPP

#include <string>

namespace Settings { struct Values; }

namespace Platform
{
// An empty selection means the user cancelled the dialog.
std::string SelectRom();
std::string DataFile(const std::string& relativePath);
void CheckDisplay();
// Explicit and portable paths are mutually exclusive. Returned paths are
// absolute UTF-8; the installed default template is never a writable target.
std::string SettingsPath(const std::string& custom, bool portable);
void EnsureSettingsDirectory(const std::string& filePath);
// Import one legacy screen-size setting without changing its source file.
// Values and sourcePath are unchanged when no file exists or an error occurs.
bool ImportLegacySettings(Settings::Values& values, std::string& sourcePath);
}

#endif
