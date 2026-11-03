#ifndef DRIVONIX_SETTINGS_HPP
#define DRIVONIX_SETTINGS_HPP

#include <array>
#include <cstddef>
#include <set>
#include <string>
#include <vector>
#include <utility>
#include "../frontend/input.hpp"

namespace Settings {

struct LibraryRoot {
    std::string id;
    std::string path;
    bool enabled = true;
    bool recursive = true;
};

struct Values {
    unsigned scale = 1;
    bool fullscreen = false;
    bool audioEnabled = true;
    unsigned volume = 100;
    bool controllersEnabled = true;
    unsigned controllerDeadzone = 25;
    int controller1 = 0;
    int controller2 = 1;
    bool showBios = false;
    bool onlineMetadata = true;
    unsigned windowWidth = 1280;
    unsigned windowHeight = 720;
    bool integerFit = true;
    std::string theme = "retronix";
    unsigned uiScale = 100;
    std::vector<LibraryRoot> libraryRoots;
    Input::Profiles bindings = Input::DefaultProfiles();
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
        std::string key;
        std::size_t valueStart = 0;
        std::size_t valueLength = 0;
        bool header = false;
    };
    Values values_;
    Values original_;
    std::vector<Line> lines_;
    std::set<std::pair<std::string,std::string>> present_;
    std::string newline_;
    bool bom_;
    std::string path_;
    std::string baseline_;
    bool exists_;
};

}
#endif
