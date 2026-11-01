#include "settings.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {
constexpr std::size_t Limit = 1024 * 1024;
const char* const Sections[] = {"display", "audio"};
const char* const Keys[] = {"scale", "fullscreen", "enabled", "volume"};

bool Space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v'; }
std::string Trim(const std::string& text) {
    std::size_t first = 0, last = text.size();
    while (first < last && Space(text[first])) ++first;
    while (last > first && Space(text[last - 1])) --last;
    return text.substr(first, last - first);
}
std::string Lower(std::string text) {
    for (char& c : text) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return text;
}
[[noreturn]] void ParseError(std::size_t line, const std::string& key, const char* why) {
    throw std::runtime_error("settings line " + std::to_string(line) +
                             (key.empty() ? "": " (" + key + ")") + ": " + why);
}
bool Boolean(const std::string& text, std::size_t line, const std::string& key) {
    const std::string value = Lower(text);
    if (value == "true" || value == "yes" || value == "on" || value == "1") return true;
    if (value == "false" || value == "no" || value == "off" || value == "0") return false;
    ParseError(line, key, "expected true/false, yes/no, on/off or 1/0");
}
unsigned Number(const std::string& text, std::size_t line, const std::string& key) {
    if (text.empty()) ParseError(line, key, "expected an unsigned integer");
    unsigned value = 0;
    for (char c : text) {
        if (c < '0' || c > '9' || value > 1000000) ParseError(line, key, "expected an unsigned integer");
        value = value * 10 + unsigned(c - '0');
    }
    return value;
}
std::string Value(const Settings::Values& values, int key) {
    switch (key) {
    case 0: return std::to_string(values.scale);
    case 1: return values.fullscreen ? "true": "false";
    case 2: return values.audioEnabled ? "true": "false";
    default: return std::to_string(values.volume);
    }
}

#ifdef _WIN32
std::wstring Wide(const std::string& path) {
    if (path.empty()) throw std::runtime_error("Empty settings path");
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), int(path.size()), nullptr, 0);
    if (!size) throw std::runtime_error("Settings path is not valid UTF-8: " + path);
    std::wstring result(std::size_t(size), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), int(path.size()), &result[0], size) != size)
        throw std::runtime_error("Cannot convert settings path: " + path);
    return result;
}
#endif

struct ReadResult { bool exists = false; std::string bytes; };
ReadResult Read(const std::string& path) {
    if (path.empty() || path.find('\0') != std::string::npos)
        throw std::runtime_error("Invalid settings path");
    errno = 0;
#ifdef _WIN32
    std::FILE* handle = _wfopen(Wide(path).c_str(), L"rb");
#else
    std::FILE* handle = std::fopen(path.c_str(), "rb");
#endif
    if (!handle) {
        const int error = errno;
        if (error == ENOENT) return {};
        throw std::system_error(error, std::generic_category(), "Cannot read settings: " + path);
    }
    std::unique_ptr<std::FILE, int(*)(std::FILE*)> input(handle, std::fclose);
    ReadResult result;
    result.exists = true;
    char buffer[4096];
    for (;;) {
        const auto count = std::fread(buffer, 1, sizeof(buffer), input.get());
        if (result.bytes.size() + count > Limit)
            throw std::runtime_error("Settings file exceeds 1 MiB: " + path);
        result.bytes.append(buffer, count);
        if (count < sizeof(buffer)) {
            if (std::ferror(input.get()))
                throw std::runtime_error("Cannot read settings: " + path);
            break;
        }
    }
    return result;
}

void CheckBaseline(const std::string& path, bool existed, const std::string& baseline) {
    const auto current = Read(path);
    if (current.exists != existed || (existed && current.bytes != baseline))
        throw std::runtime_error("Settings changed on disk; reload before saving: " + path);
}
struct Temporary {
    std::string path;
    bool created = false;
    ~Temporary() {
        if (!created) return;
#ifdef _WIN32
        try { DeleteFileW(Wide(path).c_str()); } catch (...) {}
#else
        std::remove(path.c_str());
#endif
    }
};

void WriteTemporary(Temporary& temp, const std::string& path, const std::string& bytes) {
    static std::atomic<unsigned long> counter{0};
    for (unsigned attempt = 0; attempt < 32; ++attempt) {
        temp.path = path + ".tmp." + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                    "." + std::to_string(counter.fetch_add(1));
#ifdef _WIN32
        HANDLE file = CreateFileW(Wide(temp.path).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            const DWORD error = GetLastError();
            if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS) continue;
            throw std::system_error(int(error), std::system_category(), "Cannot create settings temporary file: " + path);
        }
        temp.created = true;
        DWORD written = 0;
        const bool complete = WriteFile(file, bytes.data(), DWORD(bytes.size()), &written, nullptr) != 0 &&
                              written == bytes.size() && FlushFileBuffers(file) != 0;
        const bool closed = CloseHandle(file) != 0;
#else
        const int file = open(temp.path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (file < 0) {
            if (errno == EEXIST) continue;
            throw std::system_error(errno, std::generic_category(), "Cannot create settings temporary file: " + path);
        }
        temp.created = true;
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            const auto written = write(file, bytes.data() + offset, bytes.size() - offset);
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) break;
            offset += std::size_t(written);
        }
        const bool complete = offset == bytes.size() && fsync(file) == 0;
        const bool closed = close(file) == 0;
#endif
        if (!complete || !closed) throw std::runtime_error("Cannot flush settings: " + path);
        return;
    }
    throw std::runtime_error("Cannot reserve a settings temporary file: " + path);
}

void Replace(Temporary& temp, const std::string& path, bool existing) {
#ifdef _WIN32
    const DWORD flags = MOVEFILE_WRITE_THROUGH | (existing ? MOVEFILE_REPLACE_EXISTING: 0);
    if (!MoveFileExW(Wide(temp.path).c_str(), Wide(path).c_str(), flags))
        throw std::system_error(int(GetLastError()), std::system_category(), "Cannot replace settings: " + path);
#else
    // New documents use an exclusive link: an editor creating the file after
    // the last baseline check is never silently overwritten.
    const int result = existing ? std::rename(temp.path.c_str(), path.c_str()):link(temp.path.c_str(), path.c_str());
    if (result != 0)
        throw std::system_error(errno, std::generic_category(), "Cannot replace settings: " + path);
    if (!existing && std::remove(temp.path.c_str()) != 0) return; // The guard retries cleanup after commit.
#endif
    temp.created = false;
}

void SyncDirectory(const std::string& path) {
#ifndef _WIN32
    const auto slash = path.find_last_of('/');
    const std::string parent = slash == std::string::npos ? ".":slash == 0 ? "/":path.substr(0, slash);
    const int directory = open(parent.c_str(), O_RDONLY | O_DIRECTORY);
    if (directory < 0)
        throw std::system_error(errno, std::generic_category(), "Cannot open settings directory: " + parent);
    const bool synced = fsync(directory) == 0;
    const bool closed = close(directory) == 0;
    if (!synced || !closed) throw std::runtime_error("Cannot synchronize settings directory: " + parent);
#else
    (void)path;
#endif
}
}

namespace Settings {
void Validate(const Values& values) {
    if (values.scale < 1 || values.scale > 8) throw std::invalid_argument("display.scale must be between 1 and 8");
    if (values.volume > 100) throw std::invalid_argument("audio.volume must be between 0 and 100");
}

Document::Document() : present_{{false, false, false, false}}, newline_("\n"), bom_(false), exists_(false) {}

Document Document::Parse(const std::string& text) {
    if (text.size() > Limit) throw std::runtime_error("Settings file exceeds 1 MiB");
    const auto nul = text.find('\0');
    if (nul != std::string::npos)
        ParseError(1 + std::size_t(std::count(text.begin(), text.begin() + nul, '\n')), "",
                   "NUL bytes are not valid INI text; use UTF-8");
    Document result;
    result.bom_ = text.compare(0, 3, "\xef\xbb\xbf") == 0;
    std::size_t cursor = result.bom_ ? 3:0;
    std::string section;
    bool foundNewline = false;
    std::size_t number = 0;
    while (cursor < text.size()) {
        ++number;
        Line line;
        const auto newline = text.find('\n', cursor);
        const auto end = newline == std::string::npos ? text.size():newline;
        std::size_t contentEnd = end;
        if (newline != std::string::npos) {
            if (contentEnd > cursor && text[contentEnd - 1] == '\r') { --contentEnd; line.ending = "\r\n"; }
            else line.ending = "\n";
            if (!foundNewline) { result.newline_ = line.ending; foundNewline = true; }
        }
        line.text = text.substr(cursor, contentEnd - cursor);
        cursor = newline == std::string::npos ? text.size():newline + 1;
        const std::string stripped = Trim(line.text);
        if (!stripped.empty() && stripped[0] != ';' && stripped[0] != '#') {
            if (stripped[0] == '[') {
                const auto close = stripped.find(']');
                if (close == std::string::npos) ParseError(number, "", "section is missing ]");
                section = Lower(Trim(stripped.substr(1, close - 1)));
                const auto suffix = Trim(stripped.substr(close + 1));
                if (section.empty() || section.find_first_of("[]") != std::string::npos ||
                    (!suffix.empty() && suffix[0] != ';' && suffix[0] != '#'))
                    ParseError(number, "", "invalid section header");
                line.header = true;
                line.section = section;
            } else {
                const auto equal = line.text.find('=');
                if (equal == std::string::npos) ParseError(number, "", "expected key=value");
                const std::string key = Lower(Trim(line.text.substr(0, equal)));
                if (key.empty() || key.find_first_of("[];#") != std::string::npos)
                    ParseError(number, "", "invalid key");
                if (section == "display" && key == "scale") line.key = 0;
                else if (section == "display" && key == "fullscreen") line.key = 1;
                else if (section == "audio" && key == "enabled") line.key = 2;
                else if (section == "audio" && key == "volume") line.key = 3;
                if (line.key >= 0) {
                    const auto index = std::size_t(line.key);
                    const std::string fullKey = section + "." + key;
                    if (result.present_[index]) ParseError(number, fullKey, "duplicate setting");
                    result.present_[index] = true;
                    line.valueStart = equal + 1;
                    while (line.valueStart < line.text.size() && Space(line.text[line.valueStart])) ++line.valueStart;
                    auto valueEnd = line.text.find_first_of(";#", line.valueStart);
                    if (valueEnd == std::string::npos) valueEnd = line.text.size();
                    while (valueEnd > line.valueStart && Space(line.text[valueEnd - 1])) --valueEnd;
                    line.valueLength = valueEnd - line.valueStart;
                    const auto value = line.text.substr(line.valueStart, line.valueLength);
                    switch (line.key) {
                    case 0:
                        result.values_.scale = Number(value, number, fullKey);
                        if (result.values_.scale < 1 || result.values_.scale > 8) ParseError(number, fullKey, "must be between 1 and 8");
                        break;
                    case 1: result.values_.fullscreen = Boolean(value, number, fullKey); break;
                    case 2: result.values_.audioEnabled = Boolean(value, number, fullKey); break;
                    case 3:
                        result.values_.volume = Number(value, number, fullKey);
                        if (result.values_.volume > 100) ParseError(number, fullKey, "must be between 0 and 100");
                        break;
                    }
                }
            }
        }
        result.lines_.push_back(std::move(line));
    }
    result.original_ = result.values_;
    return result;
}

Document Document::Load(const std::string& path) {
    const auto input = Read(path);
    Document result;
    if (input.exists) {
        try { result = Parse(input.bytes); }
        catch (const std::exception& error) { throw std::runtime_error(path + ": " + error.what()); }
    }
    result.path_ = path;
    result.baseline_ = input.bytes;
    result.exists_ = input.exists;
    return result;
}

const Values& Document::Get() const { return values_; }
void Document::Apply(const Values& values) { Validate(values); values_ = values; }
bool Document::Exists() const { return exists_; }
const std::string& Document::Path() const { return path_; }

std::string Document::Serialize() const {
    Validate(values_);
    std::string output = bom_ ? "\xef\xbb\xbf":"";
    const std::size_t missing = std::size_t(-1);
    std::array<std::size_t, 2> last{{missing, missing}};
    for (std::size_t i = 0; i < lines_.size(); ++i)
        if (lines_[i].header)
            for (unsigned section = 0; section < 2; ++section)
                if (lines_[i].section == Sections[section]) last[section] = i;
    auto separate = [&]() { if (!output.empty() && output.back() != '\n') output += newline_; };
    auto appendMissing = [&](unsigned section) {
        for (unsigned key = section * 2; key < section * 2 + 2; ++key)
            if (!present_[key]) { separate(); output += std::string(Keys[key]) + "=" + Value(values_, int(key)) + newline_; }
    };
    std::size_t current = missing;
    for (std::size_t i = 0; i <= lines_.size(); ++i) {
        if (i == lines_.size() || lines_[i].header) {
            for (unsigned section = 0; section < 2; ++section)
                if (current != missing && current == last[section]) appendMissing(section);
            if (i == lines_.size()) break;
            current = i;
        }
        const auto& line = lines_[i];
        if (line.key >= 0 && Value(values_, line.key) != Value(original_, line.key))
            output += line.text.substr(0, line.valueStart) + Value(values_, line.key) +
                      line.text.substr(line.valueStart + line.valueLength);
        else output += line.text;
        output += line.ending;
    }
    if (lines_.empty()) output += "; drivonix settings. Changes take effect on restart.\n"
                                 "; booleans: true/false, yes/no, on/off or 1/0.\n";
    for (unsigned section = 0; section < 2; ++section) {
        if (last[section] != missing) continue;
        separate();
        if (!output.empty() && output.back() == '\n') output += newline_;
        output += std::string("[") + Sections[section] + "]" + newline_;
        appendMissing(section);
    }
    return output;
}

void Document::Save() {
    if (path_.empty()) throw std::runtime_error("Cannot save settings without a file path");
    const auto bytes = Serialize();
    if (bytes.size() > Limit) throw std::runtime_error("Settings file exceeds 1 MiB: " + path_);
    Document written = Parse(bytes);
    written.path_ = path_;
    written.baseline_ = bytes;
    written.exists_ = true;
    CheckBaseline(path_, exists_, baseline_);
    Temporary temporary;
    WriteTemporary(temporary, path_, bytes);
    CheckBaseline(path_, exists_, baseline_);
    Replace(temporary, path_, exists_);
    // Replacement has committed. Keep that baseline even if directory fsync
    // subsequently reports a durability error, allowing an explicit retry.
    *this = std::move(written);
    SyncDirectory(path_);
}
}
