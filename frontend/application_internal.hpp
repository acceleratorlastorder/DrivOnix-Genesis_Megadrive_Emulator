#ifndef DRIVONIX_APPLICATION_INTERNAL_HPP
#define DRIVONIX_APPLICATION_INTERNAL_HPP
#include "application.hpp"
#include "session.hpp"
#include "../cathoderaytube/crt.hpp"
#include <iostream>
#include <functional>
#include "ui.hpp"
#include "../platform/platform.hpp"
#include "../platform/storage.hpp"
#include "../platform/archive.hpp"
#include "../platform/audiooutput.hpp"
#include "../platform/settings.hpp"
#include "../library/library.hpp"
#include "../metadata/service.hpp"
#include "../thirdparty/json/json.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <future>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif


namespace Frontend { namespace Detail {
using Clock=std::chrono::steady_clock;
using Id=Library::Id;
enum class Page { Library,Details,Copies,Archive,Browser,Paused,Settings,Bindings,Capture,
                  Conflict,Text,Filters,Match,About,Error,Loading,Unsaved,SaveFailure };
enum class BrowserMode { Root,Game,Artwork };
struct TextureDelete{void operator()(sfTexture* value)const{if(value)sfTexture_destroy(value);}};
using Texture=std::unique_ptr<sfTexture,TextureDelete>;
struct DirectoryResult {std::string path,error;std::vector<Storage::File> files;};
std::string Lower(std::string text);
bool Ends(const std::string& text,const std::string& ending);
bool RomFile(const std::string& path);
std::string BaseTitle(std::string title);
std::string Version();std::string Home();
bool Starts(const std::string& value,const std::string& prefix);
std::string TimeText(uint64_t seconds);
class Application {
public:
    explicit Application(Options options):options_(std::move(options)),session_(true){}
    ~Application(){
        if(loadCancel_)loadCancel_->store(true);
        if(archiveCancel_)archiveCancel_->store(true);
        if(loading_.valid())loading_.wait();
        if(inspecting_.valid())inspecting_.wait();
        metadata_.reset();index_.reset();AudioOutput::Shutdown();CRT::Shutdown();
        if(window_)sfRenderWindow_destroy(window_);
    }
    int Run();
private:
    void RecreateWindow(bool initial=false);
    void LoadSettings();
    void ConfigureLibrary(bool scan);
    void SaveSettings();
    void RefreshQuery(bool reset=true);
    void Events();
    void Update();
    void Draw(const Input::UiActions& actions);
    void Dispatch(const std::string& action);
    bool DispatchSettings(const std::string& action);
    void Header(const std::string& title);
    void Footer(const std::string& text);
    void DrawLibrary();void DrawDetails();void DrawCopies();void DrawArchive();
    void DrawBrowser();void DrawPaused();void DrawSettings();void DrawBindings();
    void DrawTextEditor();void DrawFilters();void DrawMatch();void DrawError();
    void SetPage(Page page,bool resetFocus=true);
    void Back();
    void EditText(const std::string& title,const std::string& value,std::function<void(std::string)> finish,Page back);
    void OpenBrowser(BrowserMode mode,const std::string& path=std::string());
    void ReadDirectory(const std::string& path);
    void OpenGame(const std::string& path,const std::string& member=std::string(),bool exact=false,Id copy=0);
    void Launch(const Library::Copy& copy);
    void BeginLoad(const std::string& path,const std::string& member,Id copy=0);
    void ResumeGame();void ReturnLibrary();void RequestQuit();void RecordEnd(const std::string& outcome);
    std::function<void()> CaptureEnd(const std::string& outcome);
    void Error(const std::string& title,const std::string& message,Page back);
    void SelectGame(Id id);
    void SetTitle();
    void ApplyMetadata(const Metadata::Game& match,bool manual);
    void UpdateMetadata();
    bool DraftDirty() const;
    void FinishCapture(const Input::CaptureResult& result);

    Options options_;
    std::unique_ptr<Settings::Document> document_;
    Settings::Values settings_,draft_;
    sfRenderWindow* window_=nullptr;
    Ui::Canvas ui_;
    Texture branding_;
    std::map<std::string,Texture> covers_;
    std::unique_ptr<Library::Index> index_;
    std::unique_ptr<Metadata::Service> metadata_;
    std::shared_ptr<const Library::Snapshot> snapshot_;
    Library::Query query_;
    std::string dataDir_,cacheDir_,version_,notice_,libraryError_;
    std::string errorTitle_,errorMessage_,settingsError_,openPath_,archivePath_;
    Page page_=Page::Library,errorBack_=Page::Library,settingsBack_=Page::Library,textBack_=Page::Library;
    Session session_;
    Input::Router router_;
    Input::Frame frame_;
    Input::Capture capture_;
    Input::Device bindingDevice_=Input::Device::Keyboard;
    unsigned bindingPlayer_=0,bindingAction_=0,settingsTab_=0;
    std::string captured_,captureStatus_;
    std::vector<Archive::MemberInfo> members_;
    std::future<Archive::Inspection> inspecting_;
    std::future<PreparedGame> loading_;
    Process::CancelToken archiveCancel_,loadCancel_;
    std::future<DirectoryResult> directoryJob_;
    DirectoryResult directory_;
    std::string browserPath_;
    BrowserMode browserMode_=BrowserMode::Game;
    Page browserBack_=Page::Library;
    std::size_t listOffset_=0,browserOffset_=0,archiveOffset_=0;
    Library::Game selected_;
    std::size_t variant_=0;
    Id pendingCopy_=0,activeCopy_=0;
    Library::Locator activeLocator_;
    bool quit_=false,neutralResume_=false,mouseMoved_=false,mouseClicked_=false,settingsPreview_=false;
    bool metadataEnabled_=false;
    bool configDefaultsOnce_=false,recorded_=false,wasFocused_=true;
    bool discardExit_=false,confirmDiscard_=false;
    std::string pendingStop_,pendingPath_,pendingMember_;
    std::function<void()> retry_;
    int exitStatus_=0,wheel_=0;
    sfVector2i pointerPixels_{-100,-100};
    std::vector<sfUint32> entered_;
    Clock::time_point start_=Clock::now(),nextFrame_=Clock::now();
    std::string textTitle_,textValue_;
    std::function<void(std::string)> textFinish_;
    bool textUpper_=true,textSymbols_=false;
    std::vector<Metadata::Game> matches_;
    std::set<Id> matched_;
    const Metadata::Catalog* catalogSeen_=nullptr;
    std::string coverViewport_;
    uint64_t seenRevision_=0;
};

}}
#endif
