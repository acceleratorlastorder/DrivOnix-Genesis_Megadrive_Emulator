#include "settings.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <memory>
#include <map>
#include <set>
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
using Key = std::pair<std::string,std::string>;
using CanonicalValues = std::map<Key,std::string>;
const Key Scalars[]={{"display","scale"},{"display","fullscreen"},{"audio","enabled"},{"audio","volume"},
    {"input","enabled"},{"input","deadzone"},{"input","player1"},{"input","player2"},
    {"interface","show_bios"},{"metadata","online"},{"interface","window_width"},{"interface","window_height"},{"display","integer_fit"}};
bool Space(char c) {return c==' '||c=='\t'||c=='\r'||c=='\f'||c=='\v';}
std::string Trim(const std::string& text) {
    std::size_t first=0,last=text.size();while(first<last&&Space(text[first]))++first;while(last>first&&Space(text[last-1]))--last;
    return text.substr(first,last-first);
}
std::string Lower(std::string text) {for(char& c:text)if(c>='A'&&c<='Z')c=char(c-'A'+'a');return text;}
[[noreturn]] void ParseError(std::size_t line,const std::string& key,const char* why) {
    throw std::runtime_error("settings line "+std::to_string(line)+(key.empty()?"":" ("+key+")")+": "+why);
}
bool Boolean(const std::string& text,std::size_t line,const std::string& key) {
    const auto value=Lower(text);if(value=="true"||value=="yes"||value=="on"||value=="1")return true;
    if(value=="false"||value=="no"||value=="off"||value=="0")return false;
    ParseError(line,key,"expected true/false, yes/no, on/off or 1/0");
}
unsigned Number(const std::string& text,std::size_t line,const std::string& key) {
    if(text.empty())ParseError(line,key,"expected an unsigned integer");
    unsigned value=0;
    for(char c:text){if(c<'0'||c>'9'||value>1000000)ParseError(line,key,"expected an unsigned integer");value=value*10+unsigned(c-'0');}
    return value;
}
bool Utf8Text(const std::string& text) {
    for(std::size_t i=0;i<text.size();) {
        const auto c=static_cast<unsigned char>(text[i++]);if(c<0x80)continue;
        unsigned count=0,value=0,minimum=0;
        if(c>=0xc2&&c<=0xdf){count=1;value=c&31;minimum=0x80;}
        else if(c>=0xe0&&c<=0xef){count=2;value=c&15;minimum=0x800;}
        else if(c>=0xf0&&c<=0xf4){count=3;value=c&7;minimum=0x10000;}else return false;
        if(i+count>text.size())return false;
        for(unsigned n=0;n<count;++n){const auto next=static_cast<unsigned char>(text[i++]);if((next&0xc0)!=0x80)return false;value=(value<<6)|(next&63);}
        if(value<minimum||value>0x10ffff||(value>=0xd800&&value<=0xdfff))return false;
    }
    return true;
}
std::string Quote(const std::string& value) {
    std::string result="\"";
    for(char c:value){switch(c){case '\\':result+="\\\\";break;case '"':result+="\\\"";break;case '\n':result+="\\n";break;case '\r':result+="\\r";break;case '\t':result+="\\t";break;default:result+=c;}}
    return result+'"';
}
std::string Decode(const std::string& value,std::size_t line,const std::string& key) {
    if(value.empty()||value.front()!='"')return value;
    if(value.size()<2||value.back()!='"')ParseError(line,key,"unterminated quoted string");
    std::string result;
    for(std::size_t i=1;i+1<value.size();++i) {
        char c=value[i];
        if(c=='\\'){if(++i+1>=value.size())ParseError(line,key,"incomplete string escape");c=value[i];
            if(c=='n')c='\n';else if(c=='r')c='\r';else if(c=='t')c='\t';else if(c!='\\'&&c!='"')ParseError(line,key,"unknown string escape; use \\\\ for a backslash");}
        else if(c=='"')ParseError(line,key,"unexpected quote in string");
        result+=c;
    }
    return result;
}
int Scalar(const Key& key){for(unsigned i=0;i<sizeof(Scalars)/sizeof(Scalars[0]);++i)if(key==Scalars[i])return int(i);return -1;}
bool RootId(const std::string& id) {
    if(id.empty()||id.size()>64)return false;
    for(char c:id)if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-'))return false;
    return true;
}
bool RootSection(const std::string& section){return section.compare(0,8,"library.")==0;}
bool BindingSection(const std::string& section,unsigned& player,Input::Device& device) {
    for(unsigned p=0;p<2;++p)for(const auto d:{Input::Device::Keyboard,Input::Device::Controller})
        if(section==std::string(d==Input::Device::Keyboard?"keyboard.player":"controller.player")+std::to_string(p+1)){player=p;device=d;return true;}
    return false;
}
bool Known(const Key& key) {
    if(Scalar(key)>=0)return true;
    if(RootSection(key.first))return key.second=="path"||key.second=="enabled"||key.second=="recursive";
    unsigned player=0;Input::Device device=Input::Device::Keyboard;
    if(BindingSection(key.first,player,device))for(unsigned a=0;a<Input::ActionCount;++a)if(key.second==Input::ActionName(a))return true;
    return false;
}
CanonicalValues Canonical(const Settings::Values& v) {
    CanonicalValues result;
    const auto boolean=[](bool b){return b?std::string("true"):std::string("false");};
    const std::string values[]={std::to_string(v.scale),boolean(v.fullscreen),boolean(v.audioEnabled),std::to_string(v.volume),
        boolean(v.controllersEnabled),std::to_string(v.controllerDeadzone),std::to_string(v.controller1),std::to_string(v.controller2),
        boolean(v.showBios),boolean(v.onlineMetadata),std::to_string(v.windowWidth),std::to_string(v.windowHeight),boolean(v.integerFit)};
    for(unsigned i=0;i<sizeof(Scalars)/sizeof(Scalars[0]);++i)result.emplace(Scalars[i],values[i]);
    for(unsigned player=0;player<2;++player)for(const auto device:{Input::Device::Keyboard,Input::Device::Controller}) {
        const auto section=std::string(device==Input::Device::Keyboard?"keyboard.player":"controller.player")+std::to_string(player+1);
        const auto& bindings=device==Input::Device::Keyboard?v.bindings[player].keyboard:v.bindings[player].controller;
        for(unsigned a=0;a<Input::ActionCount;++a)result.emplace(Key{section,Input::ActionName(a)},Quote(Input::NormalizeBinding(bindings[a],device)));
    }
    for(const auto& root:v.libraryRoots){const auto section="library."+root.id;result[{section,"path"}]=Quote(root.path);
        result[{section,"enabled"}]=boolean(root.enabled);result[{section,"recursive"}]=boolean(root.recursive);}
    return result;
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
    if(values.scale<1||values.scale>8)throw std::invalid_argument("display.scale must be between 1 and 8");
    if(values.volume>100)throw std::invalid_argument("audio.volume must be between 0 and 100");
    if(values.controllerDeadzone<1||values.controllerDeadzone>95)throw std::invalid_argument("input.deadzone must be between 1 and 95");
    if(values.controller1<-1||values.controller1>7||values.controller2<-1||values.controller2>7)
        throw std::invalid_argument("input.player1 and input.player2 must be between -1 and 7");
    if(values.windowWidth<640||values.windowWidth>7680||values.windowHeight<360||values.windowHeight>4320)
        throw std::invalid_argument("interface.window_width/height must be within 640x360 and 7680x4320");
    if(values.libraryRoots.size()>256)throw std::invalid_argument("At most 256 library roots are supported");
    std::set<std::string> ids;
    for(const auto& root:values.libraryRoots) {
        if(!RootId(root.id)||!ids.insert(root.id).second)throw std::invalid_argument("Library root ids must be unique lowercase letters, numbers, hyphens or underscores (1..64 characters)");
        if(root.path.empty()||root.path.size()>32768||!Utf8Text(root.path))throw std::invalid_argument("library."+root.id+".path must be nonempty UTF-8 text");
        for(unsigned char c:root.path)if(c<0x20||c==0x7f)throw std::invalid_argument("library."+root.id+".path cannot contain control characters");
    }
    Input::ValidateProfiles(values.bindings);
}

Document::Document() : present_{},newline_("\n"),bom_(false),exists_(false) {}

Document Document::Parse(const std::string& text) {
    if(text.size()>Limit)throw std::runtime_error("Settings file exceeds 1 MiB");
    const auto nul=text.find('\0');
    if(nul!=std::string::npos)ParseError(1+std::size_t(std::count(text.begin(),text.begin()+nul,'\n')),"","NUL bytes are not valid INI text; use UTF-8");
    Document result;result.bom_=text.compare(0,3,"\xef\xbb\xbf")==0;
    std::size_t cursor=result.bom_?3:0,number=0;std::string section;bool foundNewline=false;
    while(cursor<text.size()) {
        ++number;Line line;const auto newline=text.find('\n',cursor);const auto end=newline==std::string::npos?text.size():newline;
        auto contentEnd=end;
        if(newline!=std::string::npos){if(contentEnd>cursor&&text[contentEnd-1]=='\r'){--contentEnd;line.ending="\r\n";}else line.ending="\n";
            if(!foundNewline){result.newline_=line.ending;foundNewline=true;}}
        line.text=text.substr(cursor,contentEnd-cursor);cursor=newline==std::string::npos?text.size():newline+1;
        const auto stripped=Trim(line.text);
        if(!stripped.empty()&&stripped[0]!=';'&&stripped[0]!='#') {
            if(stripped[0]=='[') {
                const auto close=stripped.find(']');if(close==std::string::npos)ParseError(number,"","section is missing ]");
                section=Lower(Trim(stripped.substr(1,close-1)));const auto suffix=Trim(stripped.substr(close+1));
                if(section.empty()||section.find_first_of("[]")!=std::string::npos||(!suffix.empty()&&suffix[0]!=';'&&suffix[0]!='#'))ParseError(number,"","invalid section header");
                if(RootSection(section)&&!RootId(section.substr(8)))ParseError(number,section,"invalid library root id");
                line.header=true;line.section=section;
            } else {
                const auto equal=line.text.find('=');if(equal==std::string::npos)ParseError(number,"","expected key=value");
                const auto key=Lower(Trim(line.text.substr(0,equal)));
                if(key.empty()||key.find_first_of("[];#")!=std::string::npos)ParseError(number,"","invalid key");
                line.section=section;
                const Key setting{section,key};
                if(Known(setting)) {
                    line.key=key;const auto fullKey=section+"."+key;
                    if(!result.present_.insert(setting).second)ParseError(number,fullKey,"duplicate setting");
                    line.valueStart=equal+1;while(line.valueStart<line.text.size()&&Space(line.text[line.valueStart]))++line.valueStart;
                    std::size_t valueEnd=line.valueStart;
                    if(valueEnd<line.text.size()&&line.text[valueEnd]=='"') {
                        bool escaped=false,closed=false;
                        for(++valueEnd;valueEnd<line.text.size();++valueEnd){const char c=line.text[valueEnd];
                            if(escaped){escaped=false;continue;}if(c=='\\'){escaped=true;continue;}if(c=='"'){++valueEnd;closed=true;break;}}
                        if(!closed)ParseError(number,fullKey,"unterminated quoted string");
                        const auto suffix=Trim(line.text.substr(valueEnd));if(!suffix.empty()&&suffix[0]!=';'&&suffix[0]!='#')ParseError(number,fullKey,"unexpected text after quoted value");
                    } else {
                        valueEnd=line.text.find_first_of(";#",line.valueStart);if(valueEnd==std::string::npos)valueEnd=line.text.size();
                        while(valueEnd>line.valueStart&&Space(line.text[valueEnd-1]))--valueEnd;
                    }
                    line.valueLength=valueEnd-line.valueStart;
                    const auto value=Decode(line.text.substr(line.valueStart,line.valueLength),number,fullKey);
                    switch(Scalar(setting)) {
                    case 0:result.values_.scale=Number(value,number,fullKey);if(result.values_.scale<1||result.values_.scale>8)ParseError(number,fullKey,"must be between 1 and 8");break;
                    case 1:result.values_.fullscreen=Boolean(value,number,fullKey);break;
                    case 2:result.values_.audioEnabled=Boolean(value,number,fullKey);break;
                    case 3:result.values_.volume=Number(value,number,fullKey);if(result.values_.volume>100)ParseError(number,fullKey,"must be between 0 and 100");break;
                    case 4:result.values_.controllersEnabled=Boolean(value,number,fullKey);break;
                    case 5:result.values_.controllerDeadzone=Number(value,number,fullKey);if(result.values_.controllerDeadzone<1||result.values_.controllerDeadzone>95)ParseError(number,fullKey,"must be between 1 and 95");break;
                    case 6:case 7:{const unsigned device=value=="-1"?0:Number(value,number,fullKey)+1;if(device>8)ParseError(number,fullKey,"must be between -1 and 7");
                        (Scalar(setting)==6?result.values_.controller1:result.values_.controller2)=int(device)-1;break;}
                    case 8:result.values_.showBios=Boolean(value,number,fullKey);break;
                    case 9:result.values_.onlineMetadata=Boolean(value,number,fullKey);break;
                    case 10:result.values_.windowWidth=Number(value,number,fullKey);break;
                    case 11:result.values_.windowHeight=Number(value,number,fullKey);break;
                    case 12:result.values_.integerFit=Boolean(value,number,fullKey);break;
                    default:
                        if(RootSection(section)) {
                            const auto id=section.substr(8);auto root=std::find_if(result.values_.libraryRoots.begin(),result.values_.libraryRoots.end(),[&](const LibraryRoot& r){return r.id==id;});
                            if(root==result.values_.libraryRoots.end()){result.values_.libraryRoots.push_back({id,"",true,true});root=result.values_.libraryRoots.end()-1;}
                            if(key=="path")root->path=value;else if(key=="enabled")root->enabled=Boolean(value,number,fullKey);else root->recursive=Boolean(value,number,fullKey);
                        } else {
                            unsigned player=0;Input::Device device=Input::Device::Keyboard;BindingSection(section,player,device);
                            auto& profile=device==Input::Device::Keyboard?result.values_.bindings[player].keyboard:result.values_.bindings[player].controller;
                            for(unsigned a=0;a<Input::ActionCount;++a)if(key==Input::ActionName(a)) {
                                try{profile[a]=Input::NormalizeBinding(value,device);}catch(const std::exception& error){ParseError(number,fullKey,error.what());}
                            }
                        }
                    }
                }
            }
        }
        result.lines_.push_back(std::move(line));
    }
    if(result.present_.count({"display","scale"})&&!result.present_.count({"interface","window_width"})&&!result.present_.count({"display","integer_fit"})) {
        result.values_.integerFit=false;result.values_.windowWidth=std::max(640u,320u*result.values_.scale);result.values_.windowHeight=std::max(360u,224u*result.values_.scale);
    }
    try{Validate(result.values_);}catch(const std::exception& error){ParseError(number?number:1,"",error.what());}
    result.original_=result.values_;return result;
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
    Validate(values_);const auto current=Canonical(values_),original=Canonical(original_);
    const bool windowChanged=values_.windowWidth!=original_.windowWidth||values_.windowHeight!=original_.windowHeight||values_.integerFit!=original_.integerFit;
    const auto required=[&](const Key& key){
        const int scalar=Scalar(key);const auto old=original.find(key),now=current.find(key);
        return lines_.empty()||(scalar>=0&&scalar<8)||(windowChanged&&(scalar==10||scalar==11||scalar==12))||old==original.end()||(now!=current.end()&&now->second!=old->second);
    };
    std::map<std::string,std::size_t> last;
    for(std::size_t i=0;i<lines_.size();++i)if(lines_[i].header)last[lines_[i].section]=i;
    std::string output=bom_?"\xef\xbb\xbf":"";
    const auto separate=[&](){if(!output.empty()&&output.back()!='\n')output+=newline_;};
    const auto appendMissing=[&](const std::string& section){for(const auto& entry:current)
        if(entry.first.first==section&&!present_.count(entry.first)&&required(entry.first)){separate();output+=entry.first.second+"="+entry.second+newline_;}};
    std::size_t header=std::size_t(-1);std::string section;
    for(std::size_t i=0;i<=lines_.size();++i) {
        if(i==lines_.size()||lines_[i].header) {
            if(header!=std::size_t(-1)&&last[section]==header)appendMissing(section);
            if(i==lines_.size())break;
            header=i;section=lines_[i].section;
        }
        const auto& line=lines_[i];
        if(!line.key.empty()) {
            const Key key{line.section,line.key};const auto now=current.find(key),old=original.find(key);
            if(now==current.end()) {
                const auto suffix=line.text.substr(line.valueStart+line.valueLength);
                const auto comment=Trim(suffix);if(!comment.empty()&&(comment[0]==';'||comment[0]=='#'))output+=suffix+line.ending;
                continue;
            }
            if(old==original.end()||now->second!=old->second)output+=line.text.substr(0,line.valueStart)+now->second+line.text.substr(line.valueStart+line.valueLength);
            else output+=line.text;
        } else output+=line.text;
        output+=line.ending;
    }
    if(lines_.empty())output+="; drivonix settings. Edit this file or use the frontend settings screen.\n; booleans: true/false, yes/no, on/off or 1/0.\n";
    std::vector<std::string> sections={"display","audio","input","interface","metadata","keyboard.player1","controller.player1","keyboard.player2","controller.player2"};
    for(const auto& root:values_.libraryRoots)sections.push_back("library."+root.id);
    for(const auto& name:sections) {
        if(last.count(name))continue;
        bool needed=false;for(const auto& entry:current)if(entry.first.first==name&&required(entry.first)&&!present_.count(entry.first))needed=true;
        if(!needed)continue;
        separate();if(!output.empty())output+=newline_;output+='['+name+']'+newline_;appendMissing(name);
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
