#include "../../frontend/application.hpp"
#include "../../genesis/genesis.hpp"
#include "../../genesis/cartridge/cartridge.hpp"
#include "../../library/library.hpp"
#include "../../platform/settings.hpp"
#include "../../platform/storage.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// This executable runs the actual Application, UI renderer, input router and
// CPU. Only the native device samples and Unicode text events are injected.
// Run from an installed/staged package or the repository root, with a display
// (xvfb-run on Linux). Usage: application_tests --report-dir DIRECTORY.
namespace {
using Frame=Frontend::AutomationFrame;
using Clock=std::chrono::steady_clock;
void Need(bool condition,const std::string& message) {
    if(!condition)throw std::runtime_error(message);
}
bool Starts(const std::string& value,const std::string& prefix) {
    return value.compare(0,prefix.size(),prefix)==0;
}
const Ui::ControlInfo* Find(const Frame& frame,const std::string& id) {
    for(const auto& item:frame.controls)if(item.id==id&&item.enabled)return &item;
    return nullptr;
}
std::string First(const Frame& frame,const std::string& prefix) {
    for(const auto& item:frame.controls)if(item.enabled&&Starts(item.id,prefix))return item.id;
    return {};
}
void Buttons(Frame& frame,unsigned buttons) {
    frame.input.devices[0].buttons=std::uint16_t(buttons);
}
std::string Describe(const Frame& frame) {
    std::ostringstream out;out<<"page="<<frame.page<<", focus="<<frame.focus
        <<", active="<<frame.active<<", paused="<<frame.paused<<", frames="<<frame.frames;
    if(!frame.error.empty())out<<", error="<<frame.error;
    return out.str();
}

class Environment {
public:
    Environment(const std::string& name,const std::string& value):name_(name) {
#ifdef _WIN32
        wideName_=Wide(name);const auto count=GetEnvironmentVariableW(wideName_.c_str(),nullptr,0);
        exists_=count!=0;
        if(count){std::vector<wchar_t> buffer(count);GetEnvironmentVariableW(wideName_.c_str(),buffer.data(),count);oldWide_=buffer.data();}
        const auto wide=Wide(value);Need(SetEnvironmentVariableW(wideName_.c_str(),wide.c_str())!=0,"Cannot isolate Windows user data");
#else
        const auto* old=std::getenv(name.c_str());exists_=old!=nullptr;if(old)old_=old;
        Need(setenv(name.c_str(),value.c_str(),1)==0,"Cannot isolate Linux user data");
#endif
    }
    ~Environment() {
#ifdef _WIN32
        SetEnvironmentVariableW(wideName_.c_str(),exists_?oldWide_.c_str():nullptr);
#else
        if(exists_)setenv(name_.c_str(),old_.c_str(),1);else unsetenv(name_.c_str());
#endif
    }
    Environment(const Environment&)=delete;Environment& operator=(const Environment&)=delete;
private:
    std::string name_,old_;bool exists_=false;
#ifdef _WIN32
    std::wstring wideName_,oldWide_;
    static std::wstring Wide(const std::string& text) {
        const auto count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),nullptr,0);
        Need(count>0,"Invalid UTF-8 environment path");std::wstring result(std::size_t(count),L'\0');
        Need(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),&result[0],count)==count,"Cannot convert environment path");return result;
    }
#endif
};

// Predict only the public spatial navigation rule. Every resulting movement
// still travels as a native D-pad sample through the real Router and Canvas.
// Breadth-first search avoids assuming a particular widget order or layout.
unsigned Route(const Frame& frame,const std::string& target) {
    const auto* current=Find(frame,frame.focus);if(!current)return 0;
    const unsigned masks[]={Controller::DpadUp,Controller::DpadDown,Controller::DpadLeft,Controller::DpadRight};
    const int xs[]={0,0,-1,1},ys[]={-1,1,0,0};
    std::deque<std::string> pending{current->id};
    std::map<std::string,unsigned> first{{current->id,0}};
    while(!pending.empty()) {
        const auto id=pending.front();pending.pop_front();const auto* item=Find(frame,id);
        for(unsigned direction=0;direction<4;++direction) {
            const Ui::ControlInfo* best=nullptr;float score=std::numeric_limits<float>::max();
            const auto x=item->area.left+item->area.width/2,y=item->area.top+item->area.height/2;
            for(const auto& candidate:frame.controls) {
                if(!candidate.enabled||candidate.id==id)continue;
                const auto dx=candidate.area.left+candidate.area.width/2-x;
                const auto dy=candidate.area.top+candidate.area.height/2-y;
                const auto forward=ys[direction]?dy*ys[direction]:dx*xs[direction];
                const auto side=ys[direction]?std::abs(dx):std::abs(dy);
                if(forward<=0.5f)continue;
                const auto cost=forward+side*4;if(cost<score){best=&candidate;score=cost;}
            }
            if(!best||first.count(best->id))continue;
            const auto button=first[id]?first[id]:masks[direction];
            if(best->id==target)return button;
            first[best->id]=button;pending.push_back(best->id);
        }
    }
    throw std::runtime_error("Control is unreachable by D-pad: "+target+" from "+frame.focus);
}

class Script {
public:
    using Operation=std::function<bool(Frame&)>;
    Script(std::string name,std::string report,unsigned initialSettle=2):name_(std::move(name)),report_(std::move(report)),settle_(initialSettle){}
    void Add(const std::string& label,Operation operation) {steps_.push_back({label,std::move(operation)});}
    void Wait(const std::string& label,std::function<bool(const Frame&)> condition) {
        Add(label,[condition](Frame& frame){return condition(frame);});
    }
    void Page(const std::string& page,const std::string& control={}) {
        Wait("wait for "+page,[page,control](const Frame& frame){return frame.page==page&&(control.empty()||Find(frame,control));});
    }
    void Select(const std::string& page,const std::string& requested,bool activate=true) {
        Add((activate?"activate ":"focus ")+requested,[page,requested,activate,release=0](Frame& frame)mutable {
            if(release){--release;return false;}
            if(frame.page!=page)return false;
            const auto target=Starts(requested,"prefix:")?First(frame,requested.substr(7)):requested;
            if(target.empty()||!Find(frame,target))return false;
            if(frame.focus==target){if(activate)Buttons(frame,Controller::A);return true;}
            const auto button=Route(frame,target);if(button){Buttons(frame,button);release=2;}return false;
        });
    }
    void Press(unsigned buttons,const std::string& label) {
        Add(label,[buttons](Frame& frame){Buttons(frame,buttons);return true;});
    }
    void Key(const std::string& key,const std::string& label) {
        Add(label,[key](Frame& frame){frame.input.keys.insert(key);return true;});
    }
    void Neutral(unsigned count=3) {
        Add("release controls",[count](Frame&)mutable{return --count==0;});
    }
    void Shot(const std::string& filename) {
        Add("screenshot "+filename,[this,filename](Frame& frame){
            const auto path=Storage::Join(report_,filename);frame.screenshotPath=path;screenshots_.push_back(path);return true;
        });
    }
    void Inject(const std::string& text) {
        Add("enter Unicode text",[text](Frame& frame){Need(frame.page=="text","Text was injected outside the real editor");frame.text=Ui::Canvas::Decode(text);return true;});
    }
    void EditBrowserPath(const std::string& path) {
        Select("browser","browserpath");Page("text","text.apply");
        Select("text","text.clear");Inject(path);Select("text","text.apply");Page("browser","browserpath");
    }
    void QuitFromLibrary() {Select("library","quit");}
    void Tick(Frame& frame) {
        // Erase physical devices/keyboard state; preserve the full native Xbox
        // representation instead of injecting high-level UiActions.
        frame.input={};frame.input.focused=true;
        auto& xbox=frame.input.devices[0];xbox.connected=true;xbox.backend=Controller::Backend::XInput;
        xbox.name="Injected Xbox acceptance controller";xbox.vendor=0x045e;xbox.product=0x028e;
        frame.text.clear();
        if(Clock::now()-started_>std::chrono::seconds(180))throw std::runtime_error(name_+" exceeded its total deadline");
        if(steps_.empty())throw std::runtime_error(name_+" stayed open after its final controller action: "+Describe(frame));
        if(Clock::now()-stepStarted_>std::chrono::seconds(20))throw std::runtime_error(name_+" timed out at "+steps_.front().label+": "+Describe(frame));
        if(settle_){--settle_;return;}
        try {
            if(steps_.front().operation(frame)) {
                const auto line=name_+": "+steps_.front().label+" ["+Describe(frame)+"]";
                std::cout<<line<<std::endl;trace_+=line+'\n';steps_.pop_front();settle_=2;stepStarted_=Clock::now();
            }
        }catch(const std::exception& error){throw std::runtime_error(name_+" / "+steps_.front().label+": "+error.what()+" ["+Describe(frame)+"]");}
    }
    void Run(Frontend::Options options) {
        options.automation=[this](Frame& frame){Tick(frame);};
        started_=stepStarted_=Clock::now();
        try {
            Need(Frontend::RunApplication(options)==0,name_+" returned a failed exit status");
            Need(steps_.empty(),name_+" closed before its controller script completed");
            for(const auto& path:screenshots_)Need(Storage::ReadBytes(path,32*1024*1024).size()>100,"Application screenshot was not written: "+path);
            WriteReport("passed\n");
        }catch(const std::exception& error){WriteReport(std::string("failed: ")+error.what()+'\n');throw;}
    }
private:
    struct Step {std::string label;Operation operation;};
    void WriteReport(const std::string& ending) {
        const auto text=trace_+ending;Storage::AtomicWrite(Storage::Join(report_,name_+".log"),std::vector<unsigned char>(text.begin(),text.end()));
    }
    std::string name_,report_,trace_;std::deque<Step> steps_;unsigned settle_=2;
    std::vector<std::string> screenshots_;Clock::time_point started_=Clock::now(),stepStarted_=Clock::now();
};

void Put(std::vector<unsigned char>& rom,unsigned at,std::uint32_t value) {
    rom[at]=static_cast<unsigned char>(value>>24);rom[at+1]=static_cast<unsigned char>(value>>16);
    rom[at+2]=static_cast<unsigned char>(value>>8);rom[at+3]=static_cast<unsigned char>(value);
}
std::vector<unsigned char> CartridgeImage() {
    std::vector<unsigned char> rom(0x4000,0);Put(rom,0,0xffff00);Put(rom,4,0x200);
    const std::string system="SEGA MEGA DRIVE ",title="DRIVONIX APPLICATION TEST";
    std::copy(system.begin(),system.end(),rom.begin()+0x100);
    std::copy(title.begin(),title.end(),rom.begin()+0x120);std::copy(title.begin(),title.end(),rom.begin()+0x150);
    rom[0x1f0]='U';rom[0x1b0]='R';rom[0x1b1]='A';Put(rom,0x1b4,0x200001);Put(rom,0x1b8,0x2003ff);
    // Disable interrupts; configure TH as output and drive low; write battery
    // RAM; repeatedly sample the actual emulated I/O port into work RAM. Under
    // TH low the Mega Drive A button is active-low bit 4.
    const unsigned char code[]={
        0x46,0xfc,0x27,0x00,
        0x13,0xfc,0x00,0x40,0x00,0xa1,0x00,0x09,
        0x13,0xfc,0x00,0x00,0x00,0xa1,0x00,0x03,
        0x13,0xfc,0x00,0xb7,0x00,0x20,0x00,0x01,
        0x10,0x39,0x00,0xa1,0x00,0x03,
        0x13,0xc0,0x00,0xff,0x00,0x00,
        0x60,0xf2};
    std::copy(std::begin(code),std::end(code),rom.begin()+0x200);return rom;
}
void Seed(const std::string& config) {
    Settings::Values values;values.onlineMetadata=false;values.audioEnabled=false;
    values.showBios=false;values.windowWidth=960;values.windowHeight=540;
    Settings::Document document;document.Apply(values);const auto text=document.Serialize();
    Storage::AtomicWrite(config,std::vector<unsigned char>(text.begin(),text.end()));
}
Frontend::Options Options(const std::string& config,const std::string& rom={}) {
    Frontend::Options options;options.configPath=config;options.customConfig=true;options.noAudio=true;
    options.noDialogs=false;options.romPath=rom;return options;
}
void Running(Script& script,std::uint64_t minimum=3) {
    script.Wait("cartridge executes",[minimum](const Frame& frame){
        return frame.page=="library"&&frame.active&&!frame.paused&&frame.frames>=minimum;
    });
}
void Freeze(Script& script,const std::string& page,unsigned count=16) {
    script.Add("paused machine stays frozen",[page,count,first=true,frames=std::uint64_t(0),cycles=std::uint64_t(0),pc=std::uint32_t(0)](Frame& frame)mutable {
        Need(frame.page==page&&frame.active&&frame.paused,"Expected a paused active cartridge");
        if(first){frames=frame.frames;cycles=Genesis::MasterCycles();pc=M68k::GetCpuState().programCounter;first=false;}
        Need(frame.frames==frames&&Genesis::MasterCycles()==cycles&&M68k::GetCpuState().programCounter==pc,"Menu advanced the emulated machine");
        return --count==0;
    });
}
void Pause(Script& script) {
    script.Press(Controller::RightThumb,"Xbox R3 opens menu");script.Page("paused","resume");
}

void GameScenario(const std::string& report,const std::string& config,const std::string& rom,const std::string& brokenDirectory) {
    Script script("controller-session",report,0);
    std::uint64_t unfocusedFrames=0;
    script.Add("lose focus on completed cartridge load",[&](Frame& frame){
        if(!frame.active||frame.page!="library"||frame.paused)return false;
        Need(frame.frames==0,"Focus acceptance missed the first completed-load frame");
        unfocusedFrames=frame.frames;frame.input.focused=false;return true;
    });
    script.Page("paused","resume");
    script.Add("unfocused completed load never runs CPU",[&](Frame& frame){
        Need(frame.active&&frame.paused&&frame.frames==unfocusedFrames&&Genesis::MasterCycles()==0,"A completed load executed while unfocused");return true;
    });
    Freeze(script,"paused");script.Press(Controller::B,"Xbox B resumes focus-paused cartridge");script.Neutral(4);Running(script);
    script.Add("cartridge initialized real I/O and SRAM",[](Frame& frame){
        Need(frame.active&&!frame.paused,"Cartridge was not running");
        Need((Genesis::M68KReadMemoryBYTE(0xff0000)&0x10)!=0,"Neutral pad did not reach the cartridge");
        Need(Cartridge::Read(0x200001)==0xb7,"Generated cartridge did not execute its battery write");return true;
    });
    Pause(script);script.Shot("controller-paused.png");Freeze(script,"paused");
    script.Select("paused","settings");script.Page("settings","s.tab.2");
    script.Select("settings","s.tab.2");script.Select("settings","s.bindings");
    script.Page("bindings","bind.controller");script.Select("bindings","bind.controller");
    script.Select("bindings","bind.action.6");script.Page("capture","capture.cancel");
    script.Neutral(4);script.Press(Controller::A,"capture native Xbox A");
    script.Page("conflict","capture.swap");script.Shot("controller-binding-conflict.png");
    script.Select("conflict","capture.swap");script.Page("bindings","bind.done");
    script.Select("bindings","bind.done");script.Page("settings","s.save");script.Select("settings","s.save");
    script.Add("binding swap saved in INI",[config](Frame& frame){
        Need(frame.page=="settings"&&frame.paused,"Saving bindings changed the session state");
        const auto saved=Settings::Document::Load(config).Get();
        Need(saved.bindings[0].controller[Input::A]=="a"&&saved.bindings[0].controller[Input::B]=="x","Conflict swap did not persist both controller bindings");
        Need(!saved.onlineMetadata,"Offline acceptance unexpectedly enabled networking");return true;
    });
    script.Shot("controller-bindings-saved.png");script.Press(Controller::B,"Xbox B returns to menu");script.Page("paused","resume");
    script.Select("paused","resume",false);
    script.Add("held resume input cannot advance game",[count=20,first=true,frames=std::uint64_t(0)](Frame& frame)mutable {
        if(first){Need(frame.page=="paused","Resume began on the wrong screen");frames=frame.frames;first=false;}
        Need(frame.active&&frame.paused&&frame.frames==frames,"Holding confirm advanced or resumed the game before neutral");
        Buttons(frame,Controller::A);return --count==0;
    });
    script.Neutral(4);Running(script);
    script.Add("native A reaches mapped Mega Drive A",[first=true,start=std::uint64_t(0)](Frame& frame)mutable {
        Need(frame.active&&!frame.paused&&frame.page=="library","Game did not resume after neutral");
        if(first){start=frame.frames;first=false;}Buttons(frame,Controller::A);
        if(frame.frames<start+3)return false;
        Need((Genesis::M68KReadMemoryBYTE(0xff0000)&0x10)==0,"Saved Xbox A binding did not reach the emulated controller port");return true;
    });
    script.Add("released Xbox A returns pad to neutral",[first=true,start=std::uint64_t(0)](Frame& frame)mutable {
        if(first){start=frame.frames;first=false;}if(frame.frames<start+2)return false;
        Need((Genesis::M68KReadMemoryBYTE(0xff0000)&0x10)!=0,"Controller button stayed stuck after release");return true;
    });
    Pause(script);script.Select("paused","open");script.Page("browser","browserpath");
    script.EditBrowserPath(brokenDirectory);script.Page("browser","file:0");
    std::uint64_t oldFrames=0,oldCycles=0;std::uint32_t oldPc=0;
    script.Add("remember paused session before bad switch",[&](Frame& frame){
        Need(frame.active&&frame.paused,"Opening a second ROM resumed the old game");oldFrames=frame.frames;oldCycles=Genesis::MasterCycles();oldPc=M68k::GetCpuState().programCounter;return true;
    });
    script.Select("browser","file:0");script.Page("error","error.back");
    script.Add("failed switch preserves old cartridge",[&](Frame& frame){
        Need(frame.active&&frame.paused&&frame.frames==oldFrames&&Genesis::MasterCycles()==oldCycles&&M68k::GetCpuState().programCounter==oldPc,"Failed ROM switch damaged the paused CPU");
        Need(Cartridge::Read(0x200001)==0xb7,"Failed ROM switch replaced battery memory");
        Need(!frame.error.empty(),"ROM preparation failure lacked an explanation");return true;
    });
    script.Shot("controller-failed-switch.png");Freeze(script,"error");script.Select("error","error.back");script.Page("paused","resume");
    script.Press(Controller::B,"Xbox B resumes original game");script.Neutral(4);
    script.Wait("original game resumes after failed switch",[&](const Frame& frame){return frame.page=="library"&&frame.active&&!frame.paused&&frame.frames>oldFrames;});
    Pause(script);script.Select("paused","library");script.Page("library","quit");
    script.Add("return to library stops session",[](Frame& frame){Need(!frame.active,"Return to library left the session active");return true;});
    script.QuitFromLibrary();script.Run(Options(config,rom));
    const auto save=Storage::ReadBytes(rom+".srm",1024);Need(save.size()==512&&save.front()==0xb7,"Application exit did not persist cartridge SRAM");
}

void EmptyLibraryScenario(const std::string& report,const std::string& config,const std::string& directory) {
    Script script("first-library",report);script.Page("library","addrootempty");script.Shot("first-empty-library.png");
    script.Add("first library has no stale games",[](Frame& frame){Need(!frame.active&&First(frame,"game:").empty(),"Isolated first library was not empty");return true;});
    script.Select("library","addrootempty");script.Page("browser","usefolder");script.EditBrowserPath(directory);
    script.Select("browser","usefolder");script.Page("library","search");
    script.Wait("folder scan discovers generated cartridge",[](const Frame& frame){return frame.page=="library"&&!First(frame,"game:").empty()&&Find(frame,"scan");});
    script.Shot("first-indexed-library.png");
    script.Select("library","prefix:game:");script.Page("details","favorite");script.Select("details","favorite");
    script.Press(Controller::B,"Xbox B returns from game details");script.Page("library","favorites");script.Select("library","favorites");
    script.Wait("favorite survives filtered query",[](const Frame& frame){return frame.page=="library"&&!First(frame,"game:").empty();});
    script.Press(Controller::Y,"Xbox Y opens search keyboard");script.Page("text","text.key.113");
    script.Select("text","text.key.113");script.Select("text","text.key.120");script.Shot("controller-search-keyboard.png");
    script.Select("text","text.apply");script.Page("library","search");
    script.Wait("OSK search filters out nonmatching game",[count=8](const Frame& frame)mutable {
        if(frame.page!="library"||!First(frame,"game:").empty()){count=8;return false;}return --count==0;
    });
    script.Select("library","search");script.Page("text","text.clear");script.Select("text","text.clear");
    script.Inject(u8"acceptance-é");script.Select("text","text.apply");
    script.Wait("edited search restores indexed game",[](const Frame& frame){return frame.page=="library"&&!First(frame,"game:").empty();});
    script.Select("library","settings");script.Page("settings","s.tab.1");script.Select("settings","s.tab.1");
    script.Select("settings","s.volume.down");script.Select("settings","s.volume.down");
    script.Key("f11","fullscreen hotkey while volume draft is dirty");script.Neutral(3);
    script.Key("f11","return to windowed mode while retaining draft");script.Neutral(3);
    script.Add("fullscreen hotkey preserves unsaved settings",[config](Frame& frame){
        Need(frame.page=="settings","Fullscreen hotkey left the settings screen");
        const auto saved=Settings::Document::Load(config).Get();
        Need(saved.volume==100&&!saved.fullscreen,"Fullscreen hotkey unexpectedly committed the volume draft or failed to return windowed");return true;
    });
    script.Press(Controller::B,"unsaved settings prompt via Xbox B");script.Page("unsaved","s.unsaved.stay");
    script.Shot("controller-unsaved-settings.png");script.Select("unsaved","s.unsaved.stay");script.Page("settings","s.save");
    script.Select("settings","s.save");
    script.Add("folders and UI volume saved atomically",[config,directory](Frame&){
        const auto saved=Settings::Document::Load(config).Get();Need(saved.volume==90,"UI volume was not saved");
        Need(saved.libraryRoots.size()==1&&Storage::Absolute(saved.libraryRoots.front().path)==Storage::Absolute(directory),"Browser folder selection was not persisted");
        Need(!saved.onlineMetadata,"Offline library test enabled networking");return true;
    });
    script.Press(Controller::B,"leave saved settings");script.Page("library","quit");script.QuitFromLibrary();script.Run(Options(config));
    Library::Index persisted(Storage::Join(Storage::DataDirectory(),"library.sqlite"));
    Library::Query favorites;favorites.favoritesOnly=true;persisted.QueryPage(favorites);persisted.Barrier().get();
    const auto snapshot=persisted.GetSnapshot();
    Need(snapshot->ready&&snapshot->games.size()==1&&snapshot->games.front().favorite,"Favorite did not persist in the actual library database");
}

void ReopenScenario(const std::string& report,const std::string& config) {
    const auto before=Settings::Document::Load(config).Get();Need(before.volume==90&&before.libraryRoots.size()==1,"Reopen lost saved settings");
    Script script("reopened-library",report);
    script.Wait("reopened library restores indexed game",[](const Frame& frame){return frame.page=="library"&&!First(frame,"game:").empty();});
    script.Select("library","favorites");
    script.Wait("favorite persisted across application shutdown",[count=8](const Frame& frame)mutable {
        if(frame.page!="library"||First(frame,"game:").empty()){count=8;return false;}return --count==0;
    });
    script.Shot("reopened-favorite-library.png");script.Select("library","prefix:game:");script.Page("details","variant:0");
    script.Select("details","variant:0");
    // A single available copy may launch immediately; multiple-copy UI must
    // remain a real controller selection rather than a private launch call.
    script.Add("launch indexed variant with controller",[release=0](Frame& frame)mutable {
        if(frame.active)return true;
        if(frame.page=="loading")return false;
        if(frame.page!="copies")return false;
        if(release){--release;return false;}
        const auto target=First(frame,"copy:");if(target.empty())return false;
        if(frame.focus==target){Buttons(frame,Controller::A);release=2;return false;}
        const auto button=Route(frame,target);if(button){Buttons(frame,button);release=2;}return false;
    });
    Running(script);Pause(script);script.Select("paused","quit");script.Run(Options(config));
}
}

int main(int argc,char** argv) {
    try {
        std::string report;
        for(int i=1;i<argc;++i){const std::string argument=argv[i];if(argument=="--report-dir"&&i+1<argc)report=argv[++i];else throw std::runtime_error("Usage: application_tests --report-dir DIRECTORY");}
        Need(!report.empty(),"Usage: application_tests --report-dir DIRECTORY");report=Storage::Absolute(report);Storage::EnsureDirectory(report);
        const auto stamp=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        const auto run=Storage::Join(report,"run-"+std::to_string(stamp));Storage::EnsureDirectory(run);
        const auto romDirectory=Storage::Join(run,u8"roms-é-日本語"),brokenDirectory=Storage::Join(run,"failed-switch");
        Storage::EnsureDirectory(romDirectory);Storage::EnsureDirectory(brokenDirectory);
        const auto rom=Storage::Join(romDirectory,u8"acceptance-é.bin"),broken=Storage::Join(brokenDirectory,"broken.bin");
        Storage::AtomicWrite(rom,CartridgeImage());Storage::AtomicWrite(broken,CartridgeImage());Storage::AtomicWrite(broken+".srm",{1});
        const auto gameConfig=Storage::Join(run,"controller.ini"),libraryConfig=Storage::Join(run,"library.ini");Seed(gameConfig);Seed(libraryConfig);
        {
            const auto data=Storage::Join(run,"controller-user");Storage::EnsureDirectory(data);
            Environment linuxData("XDG_DATA_HOME",data),windowsData("LOCALAPPDATA",data);
            GameScenario(report,gameConfig,rom,brokenDirectory);
        }
        {
            const auto data=Storage::Join(run,"library-user");Storage::EnsureDirectory(data);
            Environment linuxData("XDG_DATA_HOME",data),windowsData("LOCALAPPDATA",data);
            EmptyLibraryScenario(report,libraryConfig,romDirectory);ReopenScenario(report,libraryConfig);
        }
        const std::string passed="Application Xbox acceptance passed: real controller routing, on-screen search, Unicode folder scan, favorites, binding conflict swap, INI persistence, CPU I/O, focus-loss pause, fullscreen draft retention, pause freeze, neutral resume, failed-switch preservation, SRAM, and reopen.\n";
        Storage::AtomicWrite(Storage::Join(report,"result.txt"),std::vector<unsigned char>(passed.begin(),passed.end()));std::cout<<passed;return 0;
    }catch(const std::exception& error){std::cerr<<"Application acceptance: "<<error.what()<<'\n';return 1;}
}
