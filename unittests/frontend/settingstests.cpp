#include "../../platform/settings.hpp"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <direct.h>
#else
#include <csignal>
#include <dirent.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
void Need(bool condition, const char* why) { if (!condition) throw std::runtime_error(why); }
void Throws(const std::function<void()>& operation, const std::string& contains = "") {
    try { operation(); }
    catch (const std::exception& error) {
        Need(contains.empty() || std::string(error.what()).find(contains) != std::string::npos,
             "Settings error omitted its expected context");
        return;
    }
    throw std::runtime_error("Expected settings operation to fail");
}
#ifdef _WIN32
std::wstring Wide(const std::string& path) {
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), int(path.size()), nullptr, 0);
    Need(count > 0, "Invalid fixture path encoding");
    std::wstring result(std::size_t(count), L'\0');
    Need(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), int(path.size()), &result[0], count) == count,
         "Cannot convert fixture path");
    return result;
}
#endif
std::FILE* Open(const std::string& path, const char* mode) {
#ifdef _WIN32
    return _wfopen(Wide(path).c_str(), mode[0] == 'w' ? L"wb":L"rb");
#else
    return std::fopen(path.c_str(), mode);
#endif
}
void Write(const std::string& path, const std::string& bytes) {
    auto* file = Open(path, "wb");
    Need(file != nullptr, "Cannot write settings test fixture");
    const bool complete = std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
    const bool closed = std::fclose(file) == 0;
    Need(complete && closed, "Cannot complete settings test fixture");
}
std::string Read(const std::string& path) {
    auto* file = Open(path, "rb");
    Need(file != nullptr, "Cannot read settings test fixture");
    std::string result;
    char bytes[4096];
    for (;;) {
        const auto count = std::fread(bytes, 1, sizeof(bytes), file);
        result.append(bytes, count);
        if (count < sizeof(bytes)) break;
    }
    const bool good = !std::ferror(file);
    const bool closed = std::fclose(file) == 0;
    Need(good && closed, "Cannot complete settings fixture read");
    return result;
}
bool Directory(const std::string& path) {
#ifdef _WIN32
    return _wmkdir(Wide(path).c_str()) == 0;
#else
    return mkdir(path.c_str(), 0700) == 0;
#endif
}
void RemoveFile(const std::string& path) {
#ifdef _WIN32
    DeleteFileW(Wide(path).c_str());
#else
    unlink(path.c_str());
#endif
}
void RemoveDirectory(const std::string& path) {
#ifdef _WIN32
    RemoveDirectoryW(Wide(path).c_str());
#else
    rmdir(path.c_str());
#endif
}
void NoTemporaryFiles(const std::string& path) {
#ifdef _WIN32
    WIN32_FIND_DATAW item;
    HANDLE search = FindFirstFileW(Wide(path + "/*").c_str(), &item);
    Need(search != INVALID_HANDLE_VALUE, "Cannot inspect settings fixture directory");
    bool clean = true;
    do { if (std::wstring(item.cFileName).find(L".tmp.") != std::wstring::npos) clean = false; }
    while (FindNextFileW(search, &item));
    FindClose(search);
#else
    DIR* directory = opendir(path.c_str());
    Need(directory != nullptr, "Cannot inspect settings fixture directory");
    bool clean = true;
    while (const auto* item = readdir(directory))
        if (std::string(item->d_name).find(".tmp.") != std::string::npos) clean = false;
    closedir(directory);
#endif
    Need(clean, "Settings save left a temporary file");
}
struct Fixture {
    std::string root;
    std::vector<std::string> files, directories;
    explicit Fixture(const std::string& base) {
        errno = 0;
        Need(Directory(base) || errno == EEXIST, "Cannot create settings fixture base");
        root = base + "/settings-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        Need(Directory(root), "Cannot reserve unique settings fixture directory");
        directories.push_back(root);
    }
    std::string File(const std::string& name) { files.push_back(root + "/" + name); return files.back(); }
    std::string Dir(const std::string& name) {
        const auto result = root + "/" + name;
        Need(Directory(result), "Cannot create settings fixture subdirectory");
        directories.push_back(result);
        return result;
    }
    ~Fixture() {
#ifndef _WIN32
        for (const auto& directory : directories) chmod(directory.c_str(), 0700);
        for (const auto& file : files) chmod(file.c_str(), 0600);
#endif
        for (auto i = files.rbegin(); i != files.rend(); ++i) RemoveFile(*i);
        for (auto i = directories.rbegin(); i != directories.rend(); ++i) RemoveDirectory(*i);
    }
};

void ParserTests() {
    using Settings::Document;
    Document defaults;
    Need(defaults.Get().scale == 1 && !defaults.Get().fullscreen && defaults.Get().audioEnabled && defaults.Get().volume == 100,
         "Settings defaults differ from shipped behavior");
    Throws([&] { defaults.Save(); }, "path");
    const auto canonical = defaults.Serialize();
    Need(Document::Parse(canonical).Get().volume == 100, "Default INI cannot be read back");
    const std::string original = "\xef\xbb\xbf; user comment\r\n[DiSpLaY] ; heading\r\n"
        " Scale = 02 ; keep scale comment\r\nFullScreen=OFF\r\n[future]\r\n"
        "quoted = \"value; # with comments\"\r\n[input]\r\nenabled=ON\r\ndeadzone=25\r\nplayer1=0\r\nplayer2=1\r\n"
        "[audio]\r\nEnabled=YES # retain style\r\n volume = 31 ; level";
    auto document = Document::Parse(original);
    Need(document.Get().scale == 2 && !document.Get().fullscreen && document.Get().audioEnabled && document.Get().volume == 31,
         "INI case/whitespace/boolean/CRLF handling failed");
    Need(document.Serialize() == original, "Loading and serializing altered a fully specified INI");
    auto values = document.Get();
    values.volume = 72;
    document.Apply(values);
    std::string changed = original;
    changed.replace(changed.find("volume = 31"), 11, "volume = 72");
    Need(document.Serialize() == changed, "Apply changed comments, unknown settings or unrelated value formatting");
    Need(Document::Parse(changed).Get().volume == 72, "Updated settings cannot be read back");
    values.scale = 0;
    Throws([&] { document.Apply(values); }, "display.scale");
    Need(document.Get().scale == 2 && document.Get().volume == 72, "Invalid Apply partially replaced live values");
    values = document.Get(); values.volume = 101;
    Throws([&] { document.Apply(values); }, "audio.volume");
    Need(document.Get().volume == 72, "Invalid volume Apply changed live values");
    const auto partial = Document::Parse("; keep\n[display]\nscale=3\n[unknown]\nanswer=42\n").Serialize();
    const auto readBack = Document::Parse(partial);
    Need(readBack.Get().scale == 3 && readBack.Get().audioEnabled && readBack.Get().volume == 100,
         "Missing keys did not retain documented defaults");
    Need(partial.find("[unknown]\nanswer=42\n") != std::string::npos, "Missing-key save dropped an unknown section");
    const std::vector<std::string> invalid = {
        "[display]\nscale=0\n", "[display]\nscale=9\n", "[display]\nscale=-1\n",
        "[display]\nscale=1.5\n", "[audio]\nvolume=101\n", "[audio]\nvolume=999999999999999\n",
        "[audio]\nenabled=perhaps\n", "[audio]\nvolume= # empty\n", "[display]\nscale=1\nSCALE=2\n",
        "[display]\nscale=1\n[DISPLAY]\nscale=2\n", "[display\nscale=1\n", "[display] broken\nscale=1\n",
        "[audio]\ninvalid line\n", "[audio]\n=1\n"
    };
    for (const auto& text : invalid) Throws([&] { Document::Parse(text); }, "line");
    Throws([&] { Document::Parse("[audio]\nvolume=oops\n"); }, "audio.volume");
    Throws([&] { Document::Parse(std::string("[audio]\nvolume=1\0bad", 21)); }, "UTF-8");
    Throws([&] { Document::Parse(std::string(1024 * 1024 + 1, '#')); }, "1 MiB");
    for (const auto& value : {"true", "yes", "on", "1"})
        Need(Document::Parse(std::string("[audio]\nenabled=") + value).Get().audioEnabled, "True boolean alias failed");
    for (const auto& value : {"false", "no", "off", "0"})
        Need(!Document::Parse(std::string("[audio]\nenabled=") + value).Get().audioEnabled, "False boolean alias failed");
    auto input = Document::Parse("; input settings\n[input]\nenabled=no\ndeadzone=31 ; drift\nplayer1=3\nplayer2=-1\nextra=keep\n");
    Need(!input.Get().controllersEnabled && input.Get().controllerDeadzone == 31 &&
         input.Get().controller1 == 3 && input.Get().controller2 == -1, "Controller INI settings did not parse");
    auto inputValues = input.Get();inputValues.controller1 = 7;inputValues.controller2 = 0;
    inputValues.controllerDeadzone = 12;inputValues.controllersEnabled = true;input.Apply(inputValues);
    const auto updatedInput = input.Serialize();
    Need(updatedInput.find("deadzone=12 ; drift") != std::string::npos && updatedInput.find("extra=keep") != std::string::npos,
         "Controller editing dropped comments or unknown keys");
    Need(Document::Parse(updatedInput).Get().controller1 == 7 && Document::Parse(updatedInput).Get().controller2 == 0,
         "Controller settings failed save/read round trip");
    for(const auto& bad : {"deadzone=0", "deadzone=96", "player1=-2", "player1=8", "player2=10000000000", "enabled=maybe", "player1=0\nPLAYER1=1"})
        Throws([&] {Document::Parse(std::string("[input]\n")+bad);}, "input.");
    inputValues.controller1 = -2;Throws([&] {input.Apply(inputValues);}, "input.player1");
    Need(input.Get().controller1 == 7, "Invalid controller Apply changed active values");
    Need(Document::Parse("[audio]\nvolume=14\n").Get().controllersEnabled,
         "Existing settings did not receive controller defaults");
}

#ifndef _WIN32
struct Mode {
    std::string path;
    mode_t restore;
    Mode(const std::string& target, mode_t next, mode_t original) : path(target), restore(original) {
        Need(chmod(path.c_str(), next) == 0, "Cannot set settings fixture permissions");
    }
    ~Mode() { chmod(path.c_str(), restore); }
};
struct SizeLimit {
    rlimit previous;
    using Handler = void(*)(int);
    Handler handler;
    SizeLimit() {
        Need(getrlimit(RLIMIT_FSIZE, &previous) == 0, "Cannot read file-size limit");
        handler = std::signal(SIGXFSZ, SIG_IGN);
        auto next = previous; next.rlim_cur = 7;
        Need(setrlimit(RLIMIT_FSIZE, &next) == 0, "Cannot set settings partial-write fixture limit");
    }
    ~SizeLimit() { setrlimit(RLIMIT_FSIZE, &previous); std::signal(SIGXFSZ, handler); }
};
#else
struct ReadLock {
    HANDLE handle;
    explicit ReadLock(const std::string& path) {
        handle = CreateFileW(Wide(path).c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        Need(handle != INVALID_HANDLE_VALUE, "Cannot reserve exclusive settings fixture lock");
    }
    ~ReadLock() { CloseHandle(handle); }
};
#endif

void FileTests(Fixture& fixture) {
    using Settings::Document;
    const auto path = fixture.File("drivonix.ini");
    auto document = Document::Load(path);
    Need(!document.Exists() && document.Path() == path, "Missing settings did not produce bound defaults");
    document.Save();
    Need(document.Exists(), "First save did not establish document baseline");
    auto values = document.Get(); values.scale = 4; values.volume = 19;
    document.Apply(values); document.Save(); document.Save();
    Need(Document::Load(path).Get().scale == 4 && Document::Load(path).Get().volume == 19,
         "Settings save/reload failed");
    const auto persisted = Read(path);
    auto stale = Document::Load(path);
    values = stale.Get(); values.volume = 35; stale.Apply(values);
    Write(path, persisted + "; manual edit\n");
    Throws([&] { stale.Save(); }, "changed on disk");
    Need(Read(path) == persisted + "; manual edit\n" && stale.Get().volume == 35,
         "Stale settings save overwrote manual edits or lost pending changes");
    const auto racing = fixture.File("created-later.ini");
    auto missing = Document::Load(racing);
    Write(racing, "[audio]\nvolume=44\n");
    Throws([&] { missing.Save(); }, "changed on disk");
    Need(Read(racing) == "[audio]\nvolume=44\n", "First save overwrote a subsequently created settings file");
    const auto parent = fixture.Dir("unicode-\xc3\xa9-\xe9\x85\x8d\xe7\xbd\xae");
    const auto unicode = fixture.File("unicode-\xc3\xa9-\xe9\x85\x8d\xe7\xbd\xae/user.ini");
    auto international = Document::Load(unicode); international.Save();
    values = international.Get(); values.fullscreen = true; values.audioEnabled = false;
    international.Apply(values); international.Save();
    Need(Document::Load(unicode).Get().fullscreen && !Document::Load(unicode).Get().audioEnabled,
         "UTF-8 settings path save/reload failed");
    NoTemporaryFiles(parent);
    const auto full = fixture.File("too-large.ini");
    Write(full, std::string(1024 * 1024 + 1, '#'));
    Throws([&] { Document::Load(full); }, "1 MiB");

    const auto deniedDirectory = fixture.Dir("denied");
    const auto denied = fixture.File("denied/user.ini");
    auto protectedDocument = Document::Load(denied); protectedDocument.Save();
    const auto previous = Read(denied);
    values = protectedDocument.Get(); values.volume = 23; protectedDocument.Apply(values);
#ifdef _WIN32
    {
        ReadLock lock(denied);
        Throws([&] { Document::Load(denied); }, "read settings");
        Throws([&] { protectedDocument.Save(); }, "read settings");
    }
    Need(Read(denied) == previous && protectedDocument.Get().volume == 23,
         "Locked-file failure replaced settings or discarded pending values");
#else
    if (geteuid() != 0) {
        { Mode mode(denied, 0000, 0600); Throws([&] { Document::Load(denied); }, "read settings"); }
        { Mode mode(deniedDirectory, 0500, 0700); Throws([&] { protectedDocument.Save(); }, "temporary file"); }
        Need(Read(denied) == previous && protectedDocument.Get().volume == 23,
             "Permission failure replaced settings or discarded pending values");
    }
    {
        SizeLimit limit;
        Throws([&] { protectedDocument.Save(); }, "flush settings");
    }
    Need(Read(denied) == previous && protectedDocument.Get().volume == 23,
         "Partial write replaced canonical settings or discarded pending values");
#endif
    NoTemporaryFiles(deniedDirectory);
    protectedDocument.Save();
    Need(Document::Load(denied).Get().volume == 23, "Retry after save failure did not persist changed settings");
#ifndef _WIN32
    if (geteuid() != 0) {
        values = protectedDocument.Get(); values.volume = 79; protectedDocument.Apply(values);
        // Write+execute permits rename; lack of read permission prevents opening
        // the parent for fsync after replacement. That is a post-commit error.
        { Mode mode(deniedDirectory, 0300, 0700); Throws([&] { protectedDocument.Save(); }, "settings directory"); }
        Need(Document::Load(denied).Get().volume == 79 && protectedDocument.Exists(),
             "Post-replacement error lost its actual persisted baseline");
        protectedDocument.Save();
        Need(Document::Load(denied).Get().volume == 79, "Post-commit failure could not be retried");
    }
#endif
    NoTemporaryFiles(deniedDirectory);
    NoTemporaryFiles(fixture.root);
}
}

int main(int argc, char** argv) {
    try {
        Need(argc == 2, "Expected settings fixture base directory");
        ParserTests();
        Fixture fixture(argv[1]);
        FileTests(fixture);
        std::cout << "INI parsing, validation, comment preservation, Unicode paths, atomic save/reload and failure/retry checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Settings test failed: " << error.what() << '\n';
        return 1;
    }
}
