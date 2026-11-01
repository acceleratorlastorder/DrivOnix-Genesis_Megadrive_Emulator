#ifndef DRIVONIX_SETTINGS_HPP
#define DRIVONIX_SETTINGS_HPP

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace Settings {

struct Values {
    unsigned scale = 1;
    bool fullscreen = false;
    bool audioEnabled = true;
    unsigned volume = 100;
    bool controllersEnabled = true;
    unsigned controllerDeadzone = 25;
    int controller1 = 0;
    int controller2 = 1;
};

void Validate(const Values& values);

// The frontend and a future settings UI edit the same validated values. Unknown
// sections, keys and comments survive a save. Files are read/saved as UTF-8.
class Document {
public:
    Document();
    static Document Parse(const std::string& text);
    // A genuinely missing file produces bound defaults; other read errors throw.
    static Document Load(const std::string& path);
    const Values& Get() const;
    void Apply(const Values& values);
    std::string Serialize() const;
    bool Exists() const;
    const std::string& Path() const;
    // The parent directory must already exist. Saving an unbound document or a
    // file with detected changes since Load/Save throws without replacing it.
    // A post-replacement directory-sync error may leave the new file installed;
    // the document then retains its new baseline so that retry remains possible.
    void Save();

private:
    struct Line {
        std::string text;
        std::string ending;
        std::string section;
        int key = -1;
        std::size_t valueStart = 0;
        std::size_t valueLength = 0;
        bool header = false;
    };
    Values values_;
    Values original_;
    std::vector<Line> lines_;
    std::array<bool, 8> present_;
    std::string newline_;
    bool bom_;
    std::string path_;
    std::string baseline_;
    bool exists_;
};

}
#endif
