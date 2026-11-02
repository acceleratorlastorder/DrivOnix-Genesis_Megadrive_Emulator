#include "application_internal.hpp"

namespace Frontend { namespace Detail {
std::string Lower(std::string text){for(auto& c:text)if(c>='A'&&c<='Z')c=char(c-'A'+'a');return text;}
bool Ends(const std::string& text,const std::string& ending){return text.size()>=ending.size()&&Lower(text.substr(text.size()-ending.size()))==ending;}
bool RomFile(const std::string& path){return Ends(path,".bin")||Ends(path,".md")||Ends(path,".gen")||Ends(path,".smd")||Archive::IsArchive(path);}
std::string BaseTitle(std::string title){const auto p=title.find(" (");const auto b=title.find(" [");auto cut=std::min(p,b);if(cut!=std::string::npos)title.resize(cut);return title;}
std::string Version(){try{auto bytes=Storage::ReadBytes(Platform::DataFile("build-info.json"),65536);
    std::unique_ptr<json_value_s,decltype(&std::free)> parsed(json_parse(bytes.data(),bytes.size()),&std::free);
    auto* object=parsed?json_value_as_object(parsed.get()):nullptr;
    if(object)for(auto* field=object->start;field;field=field->next)if(std::string(field->name->string,field->name->string_size)=="version"){
        auto* value=json_value_as_string(field->value);if(value)return std::string(value->string,value->string_size);}
    }catch(...){}return "development";}
std::string Home(){const char* home=std::getenv("HOME");
#ifdef _WIN32
    const DWORD length=GetEnvironmentVariableW(L"USERPROFILE",nullptr,0);
    if(length){std::vector<wchar_t> wide(length);GetEnvironmentVariableW(L"USERPROFILE",wide.data(),length);
        const int count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide.data(),-1,nullptr,0,nullptr,nullptr);
        if(count>1){std::string user(std::size_t(count),'\0');WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide.data(),-1,&user[0],count,nullptr,nullptr);user.pop_back();return user;}}
#endif
    return home&&*home?home:Storage::Absolute(".");}
bool Starts(const std::string& value,const std::string& prefix){return value.compare(0,prefix.size(),prefix)==0;}
std::string TimeText(uint64_t seconds){return std::to_string(seconds/3600)+"h "+std::to_string((seconds/60)%60)+"m";}


void Application::LoadSettings(){
    auto next=Settings::Document::Load(options_.configPath);
    if(!next.Exists()){
        if(!options_.customConfig){auto values=next.Get();std::string legacy;if(Platform::ImportLegacySettings(values,legacy))next.Apply(values);}
        Platform::EnsureSettingsDirectory(options_.configPath);next.Save();
    }
    settings_=next.Get();draft_=settings_;document_.reset(new Settings::Document(std::move(next)));
    settingsError_.clear();configDefaultsOnce_=false;
}
void Application::RecreateWindow(bool initial){
    const auto desktop=sfVideoMode_getDesktopMode();
    unsigned width=std::min(settings_.windowWidth,std::max(640u,desktop.width-std::min(80u,desktop.width/8)));
    unsigned height=std::min(settings_.windowHeight,std::max(360u,desktop.height-std::min(100u,desktop.height/6)));
    const auto mode=settings_.fullscreen?desktop:sfVideoMode{width,height,32};
    auto* replacement=sfRenderWindow_create(mode,"DrivOnix",settings_.fullscreen?sfFullscreen:(sfTitlebar|sfResize|sfClose),nullptr);
    if(!replacement)throw std::runtime_error("Cannot create the DrivOnix window");
    if(window_){CRT::Shutdown();sfRenderWindow_destroy(window_);}
    window_=replacement;
    sfRenderWindow_setVerticalSyncEnabled(window_,sfTrue);
    CRT::Attach(window_);SetTitle();ui_.Begin(window_,{});
    if(!initial)router_.SuppressUntilNeutral();
}
void Application::SetTitle(){
    if(!window_)return;
    std::string title="DrivOnix "+version_;
    if(session_.Active())title+=" - "+session_.Rom().title+(session_.Paused()?" [Paused]":"");
    auto unicode=Ui::Canvas::Decode(title);unicode.push_back(0);sfRenderWindow_setUnicodeTitle(window_,unicode.data());
}
void Application::ConfigureLibrary(bool scan){
    std::vector<Library::Root> roots;
    for(const auto& root:settings_.libraryRoots)if(root.enabled)roots.push_back({root.path,root.recursive,root.id});
    if(index_)index_->Configure(std::move(roots),scan&&!session_.Active());
}
void Application::RefreshQuery(bool reset){
    if(reset)query_.offset=0;query_.limit=12;if(index_)index_->QueryPage(query_);
}
void Application::SetPage(Page page,bool reset){page_=page;if(reset)ui_.ClearFocus();router_.SuppressUntilNeutral();}
void Application::Error(const std::string& title,const std::string& message,Page back){
    std::cerr<<"drivonix: "<<message<<'\n';
    if(options_.noDialogs){exitStatus_=1;quit_=true;return;}
    session_.Pause();errorTitle_=title;errorMessage_=message;errorBack_=back;SetPage(Page::Error);SetTitle();
}
void Application::EditText(const std::string& title,const std::string& value,std::function<void(std::string)> finish,Page back){
    textTitle_=title;textValue_=value;textFinish_=std::move(finish);textBack_=back;textUpper_=false;textSymbols_=false;SetPage(Page::Text);
}
bool Application::DraftDirty() const {Settings::Document a,b;a.Apply(settings_);b.Apply(draft_);return a.Serialize()!=b.Serialize();}

int Application::Run(){
    version_=Version();
    try{LoadSettings();}catch(const std::exception& error){if(options_.noDialogs)throw;settingsError_=error.what();settings_=Settings::Values();draft_=settings_;}
    // CLI automation validates selected content before requiring graphical assets.
    if(options_.noDialogs&&!options_.romPath.empty()){
        if(options_.memberProvided&&!Archive::IsArchive(options_.romPath))throw std::runtime_error("--archive-member requires a ZIP or 7z archive");
        if(!Archive::IsArchive(options_.romPath)||options_.memberProvided){
            Session::Prepare(options_.romPath,options_.member,settings_.showBios,settings_.showBios?Platform::DataFile("bios/drivonix.bios"):"");
        }
    }
    Platform::CheckDisplay();RecreateWindow(true);
    ui_.LoadFonts(Platform::DataFile("assets/fonts/unifont.otf"),Platform::DataFile("assets/fonts/unifont_upper.otf"));
    try{const auto bytes=Storage::ReadBytes(Platform::DataFile("assets/branding.bmp"),4*1024*1024);auto* image=sfImage_createFromMemory(bytes.data(),bytes.size());
        if(image){sfImage_createMaskFromColor(image,sfBlack,0);branding_.reset(sfTexture_createFromImage(image,nullptr));sfImage_destroy(image);}}catch(...){}
    dataDir_=Storage::DataDirectory(options_.portable);Storage::EnsureDirectory(dataDir_);cacheDir_=Storage::Join(dataDir_,"cache");
    index_.reset(new Library::Index(Storage::Join(dataDir_,"library.sqlite")));
    Metadata::Options online;online.enabled=settings_.onlineMetadata;metadataEnabled_=online.enabled;metadata_.reset(new Metadata::Service(cacheDir_,online));
    ConfigureLibrary(true);RefreshQuery();
    if(!settingsError_.empty())Error("Settings need attention",settingsError_,Page::Library);
    else if(!options_.romPath.empty())OpenGame(options_.romPath,options_.member,options_.memberProvided);
    while(!quit_&&sfRenderWindow_isOpen(window_)){
        Events();Update();
        const auto now=Clock::now();
        const auto ms=uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(now-start_).count());
        frame_.devices=Controller::ReadDevices();frame_.focused=sfRenderWindow_hasFocus(window_)!=sfFalse;
        std::string screenshot;
        if(options_.automation){
            static const char* names[]={"library","details","copies","archive","browser","paused","settings","bindings","capture","conflict","text","filters","match","about","error","loading","unsaved","savefailure"};
            AutomationFrame sample;sample.input=frame_;sample.controls=ui_.Controls();sample.page=names[unsigned(page_)];
            sample.focus=ui_.Focus();sample.error=errorMessage_;sample.active=session_.Active();sample.paused=session_.Paused();sample.frames=session_.ActiveFrames();
            options_.automation(sample);frame_=std::move(sample.input);screenshot=std::move(sample.screenshotPath);
            entered_=std::move(sample.text);
            if(page_==Page::Text&&!entered_.empty()){
                auto text=Ui::Canvas::Decode(textValue_);
                for(auto point:entered_)if(point==8){if(!text.empty())text.pop_back();}else if(point>=32&&point!=127&&text.size()<256)text.push_back(point);
                textValue_=Ui::Canvas::Encode(text);
            }
            if(sample.requestClose)RequestQuit();
        }
        const bool playing=session_.Active()&&!session_.Paused()&&page_==Page::Library;
        auto routed=router_.Update(frame_,settings_,playing,ms);
        if(routed.ui.fullscreen&&page_!=Page::Capture&&page_!=Page::Conflict){
            if(document_){draft_=settings_;draft_.fullscreen=!draft_.fullscreen;SaveSettings();}
            else{settings_.fullscreen=!settings_.fullscreen;RecreateWindow();}
        }
        if(page_==Page::Capture){FinishCapture(capture_.Update(frame_,ms));routed.ui={};}
        if(neutralResume_&&Input::Neutral(frame_,settings_.controllerDeadzone)){neutralResume_=false;session_.Resume();nextFrame_=now;SetTitle();}
        if(playing&&routed.ui.menu){session_.Pause();neutralResume_=false;SetPage(Page::Paused);SetTitle();routed.ui={};}
        if(session_.Booting()&&playing){bool skip=frame_.keys.count("enter")||frame_.keys.count("space");
            for(const auto& device:frame_.devices)skip=skip||(device.connected&&(device.buttons&(Controller::A|Controller::Start)));
            if(skip){session_.SkipBios();router_.SuppressUntilNeutral();routed.game={{0,0}};}}
        if(session_.Active()&&!session_.Paused()&&page_==Page::Library&&now>=nextFrame_){
            try{session_.Step(routed.game);}catch(const std::exception& error){Error("Game stopped",error.what(),Page::Paused);}
            nextFrame_+=std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(session_.FrameSeconds()));
            if(now>nextFrame_+std::chrono::milliseconds(100))nextFrame_=now;
        }
        Draw(routed.ui);
        if(!screenshot.empty()){
            auto* texture=sfTexture_create(sfRenderWindow_getSize(window_).x,sfRenderWindow_getSize(window_).y);
            if(!texture)throw std::runtime_error("Cannot capture frontend screenshot");
            sfTexture_updateFromRenderWindow(texture,window_,0,0);auto* image=sfTexture_copyToImage(texture);
            const bool saved=image&&sfImage_saveToFile(image,screenshot.c_str());if(image)sfImage_destroy(image);sfTexture_destroy(texture);
            if(!saved)throw std::runtime_error("Cannot save frontend screenshot");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if(session_.Active()&&!discardExit_){
        try{session_.Stop();RecordEnd("completed");}catch(const std::exception& error){std::cerr<<"drivonix save: "<<error.what()<<'\n';exitStatus_=1;}
    }
    return exitStatus_;
}

void Application::Events(){
    mouseClicked_=mouseMoved_=false;wheel_=0;entered_.clear();
    sfEvent event;
    while(sfRenderWindow_pollEvent(window_,&event)){
        if(event.type==sfEvtClosed){RequestQuit();continue;}
        if(event.type==sfEvtResized){
            if(event.size.width<640||event.size.height<360)sfRenderWindow_setSize(window_,{std::max(640u,event.size.width),std::max(360u,event.size.height)});
        }
        if(event.type==sfEvtKeyPressed||event.type==sfEvtKeyReleased){const auto name=Input::KeyName(int(event.key.code));
            if(!name.empty()&&name!="unknown"){if(event.type==sfEvtKeyPressed)frame_.keys.insert(name);else frame_.keys.erase(name);}}
        if(event.type==sfEvtMouseMoved){pointerPixels_={event.mouseMove.x,event.mouseMove.y};mouseMoved_=true;}
        if(event.type==sfEvtMouseButtonPressed&&event.mouseButton.button==sfMouseLeft){pointerPixels_={event.mouseButton.x,event.mouseButton.y};mouseClicked_=true;}
        if(event.type==sfEvtMouseWheelScrolled)wheel_+=event.mouseWheelScroll.delta>0?1:-1;
        if(event.type==sfEvtTextEntered)entered_.push_back(event.text.unicode);
        if(event.type==sfEvtLostFocus){
            frame_.keys.clear();router_.SuppressUntilNeutral();neutralResume_=false;
            if(session_.Active()&&!session_.Paused()){session_.Pause();SetPage(Page::Paused);SetTitle();}
        }
    }
    if(page_==Page::Text){
        auto text=Ui::Canvas::Decode(textValue_);
        for(auto point:entered_){if(point==8){if(!text.empty())text.pop_back();}else if(point>=32&&point!=127&&text.size()<256)text.push_back(point);}
        textValue_=Ui::Canvas::Encode(text);
    }
    if(wheel_){
        const int delta=wheel_>0?-1:1;
        if(page_==Page::Library&&!session_.Active()){
            if(delta<0&&query_.offset>=12){query_.offset-=12;RefreshQuery(false);}
            else if(delta>0&&snapshot_&&query_.offset+12<snapshot_->totalGames){query_.offset+=12;RefreshQuery(false);}
        }else if(page_==Page::Browser){if(delta<0&&browserOffset_>=10)browserOffset_-=10;else if(delta>0&&browserOffset_+10<directory_.files.size())browserOffset_+=10;}
    }
}
void Application::OpenBrowser(BrowserMode mode,const std::string& requested){
    browserMode_=mode;browserBack_=page_;browserOffset_=0;
    SetPage(Page::Browser);ReadDirectory(requested.empty()?Home():requested);
}
void Application::ReadDirectory(const std::string& path){
    if(directoryJob_.valid()&&directoryJob_.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
    if(directoryJob_.valid())directoryJob_.get();
    browserPath_=path;browserOffset_=0;directory_.files.clear();directory_.error.clear();
    directoryJob_=std::async(std::launch::async,[path]{DirectoryResult result;result.path=path;
        try{result.files=Storage::List(path);std::sort(result.files.begin(),result.files.end(),[](const Storage::File& a,const Storage::File& b){return a.directory!=b.directory?a.directory>b.directory:Lower(Storage::Filename(a.path))<Lower(Storage::Filename(b.path));});}
        catch(const std::exception& error){result.error=error.what();}return result;});
}
void Application::OpenGame(const std::string& path,const std::string& member,bool exact,Id copy){
    session_.Pause();neutralResume_=false;if(index_)index_->CancelScan();
    if(exact&&!Archive::IsArchive(path))throw std::runtime_error("--archive-member requires a ZIP or 7z archive");
    if(Archive::IsArchive(path)&&!exact){
        archivePath_=path;archiveOffset_=0;members_.clear();pendingCopy_=copy;
        archiveCancel_=std::make_shared<std::atomic<bool>>(false);
        const auto token=archiveCancel_;inspecting_=std::async(std::launch::async,[path,token]{return Archive::Inspect(path,token);});
        SetPage(Page::Archive);return;
    }
    BeginLoad(path,member,copy);
}
void Application::BeginLoad(const std::string& path,const std::string& member,Id copy){
    session_.Pause();neutralResume_=false;if(index_)index_->CancelScan();
    if(loading_.valid())throw std::runtime_error("Another game is still being prepared");
    pendingPath_=path;pendingMember_=member;pendingCopy_=copy;
    loadCancel_=std::make_shared<std::atomic<bool>>(false);const auto token=loadCancel_;
    const bool bios=settings_.showBios;const auto biosPath=bios?Platform::DataFile("bios/drivonix.bios"):std::string();
    loading_=std::async(std::launch::async,[path,member,bios,biosPath,token]{return Session::Prepare(path,member,bios,biosPath,token);});
    SetPage(Page::Loading);SetTitle();
}
void Application::Launch(const Library::Copy& copy){
    if(!copy.available){Error("This copy is unavailable",copy.error.empty()?copy.status:copy.error,Page::Copies);return;}
    if(index_&&variant_<selected_.variants.size())index_->SetPreferredCopy(selected_.variants[variant_].id,copy.id);
    BeginLoad(copy.locator.path,copy.locator.member,copy.id);
}
void Application::ResumeGame(){SetPage(Page::Library);session_.Pause();neutralResume_=true;SetTitle();}
std::function<void()> Application::CaptureEnd(const std::string& outcome){
    if(recorded_||!index_)return []{};
    const auto seconds=uint64_t(session_.ActiveFrames()*session_.FrameSeconds());
    const auto copy=activeCopy_;const auto locator=activeLocator_;
    RomInfo::Prepared rom;const auto& source=session_.Rom();
    rom.region=source.region;rom.title=source.title;rom.saveIdentity=source.saveIdentity;rom.sha256=source.sha256;
    rom.size=source.size;rom.crc32=source.crc32;rom.header=source.header;
    return [this,copy,locator,rom,seconds,outcome]{
        if(copy)index_->RecordSession(copy,seconds,outcome);
        else if(!locator.path.empty())index_->RecordExternal(locator,rom,seconds,outcome);
    };
}
void Application::RecordEnd(const std::string& outcome){CaptureEnd(outcome)();recorded_=true;}
void Application::ReturnLibrary(){
    try{session_.Stop();RecordEnd("completed");neutralResume_=false;SetPage(Page::Library);SetTitle();ConfigureLibrary(true);RefreshQuery();}
    catch(const std::exception& error){pendingStop_="library";errorMessage_=error.what();confirmDiscard_=false;SetPage(Page::SaveFailure);}
}
void Application::RequestQuit(){
    const bool editing=page_==Page::Settings||page_==Page::Bindings||page_==Page::Capture||page_==Page::Conflict||page_==Page::Unsaved||
        (page_==Page::Text&&(textBack_==Page::Settings||textBack_==Page::Bindings))||
        (page_==Page::Browser&&browserBack_==Page::Settings);
    if(editing&&DraftDirty()){pendingStop_="settingsquit";SetPage(Page::Unsaved);return;}
    if(loadCancel_)loadCancel_->store(true);if(archiveCancel_)archiveCancel_->store(true);
    try{if(session_.Active()){session_.Stop();RecordEnd("completed");}quit_=true;}
    catch(const std::exception& error){if(options_.noDialogs){std::cerr<<error.what()<<'\n';exitStatus_=1;quit_=true;return;}
        pendingStop_="quit";errorMessage_=error.what();confirmDiscard_=false;SetPage(Page::SaveFailure);}
}
void Application::SaveSettings(){
    const auto back=page_;const auto previous=settings_;const bool resume=session_.Active()&&!session_.Paused();
    session_.Pause();
    try{
        Settings::Validate(draft_);
        if(!document_)throw std::runtime_error("Repair and reload the settings file before saving. Your original file has not been replaced.");
        auto next=*document_;next.Apply(draft_);next.Save();*document_=std::move(next);settings_=draft_;
        if(previous.fullscreen!=settings_.fullscreen||previous.windowWidth!=settings_.windowWidth||previous.windowHeight!=settings_.windowHeight){
            try{RecreateWindow();}catch(...){settings_.fullscreen=false;settings_.windowWidth=1280;settings_.windowHeight=720;RecreateWindow();throw;}
        }
        auto host=settings_;if(options_.noAudio)host.audioEnabled=false;AudioOutput::ApplySettings(host);
        if(!session_.Active()||session_.Paused())AudioOutput::Suspend();
        ConfigureLibrary(!session_.Active());Metadata::Options online;online.enabled=settings_.onlineMetadata&&!session_.Active();metadata_->Configure(online);metadataEnabled_=online.enabled;
        notice_="SETTINGS SAVED";SetPage(back);if(resume)ResumeGame();
    }catch(const std::exception& error){settings_=previous;retry_=[this]{SaveSettings();};Error("Settings were not applied",error.what(),back);}
}
void Application::SelectGame(Id id){
    if(!snapshot_)return;
    const auto game=std::find_if(snapshot_->games.begin(),snapshot_->games.end(),[&](const Library::Game& item){return item.id==id;});
    if(game==snapshot_->games.end())return;selected_=*game;variant_=0;listOffset_=0;
    for(std::size_t i=0;i<selected_.variants.size();++i)if(selected_.variants[i].id==selected_.preferredVariant){variant_=i;listOffset_=(i/9)*9;break;}
    SetPage(Page::Details);ui_.Focus("variant:"+std::to_string(variant_));
}
void Application::ApplyMetadata(const Metadata::Game& match,bool manual){
    Library::Metadata value;value.catalogId=match.id;value.title=BaseTitle(match.title);value.region=match.region;
    value.developer=match.developer;value.publisher=match.publisher;value.genre=match.genre;
    value.year=int(match.year);value.players=int(match.players);value.artworkPath=selected_.metadata.artworkPath;
    if(index_)index_->ApplyMetadata(selected_.id,value,manual);
    selected_.metadata=value;coverViewport_.clear();
}
void Application::UpdateMetadata(){
    if(!metadata_||!snapshot_)return;
    const bool enabled=settings_.onlineMetadata&&!session_.Active();
    if(enabled!=metadataEnabled_){Metadata::Options online;online.enabled=enabled;metadata_->Configure(online);metadataEnabled_=enabled;}
    if(session_.Active())return;
    const auto catalog=metadata_->Snapshot();
    if(catalog.get()!=catalogSeen_){
        catalogSeen_=catalog.get();coverViewport_.clear();
        if(catalog){std::vector<Library::CatalogRecord> records;
            for(const auto& game:catalog->Games())if(game.hasChecksum){Library::Metadata value;
                value.catalogId=game.id;value.title=BaseTitle(game.title);value.region=game.region;
                value.developer=game.developer;value.publisher=game.publisher;value.genre=game.genre;
                value.year=int(game.year);value.players=int(game.players);records.push_back({game.crc32,game.size,std::move(value)});}
            index_->ApplyCatalog(std::move(records));}
    }
    std::vector<Metadata::Service::CoverRequest> covers;
    auto request=[&](const Library::Game& game){
        std::string title;
        if(catalog){const auto* match=catalog->Find(game.metadata.catalogId);if(match)title=match->title;}
        covers.push_back({std::to_string(game.id),title,game.metadata.artworkPath});
    };
    if(page_==Page::Details||page_==Page::Copies||page_==Page::Match)request(selected_);
    else if(page_==Page::Library)for(const auto& game:snapshot_->games)request(game);
    std::string signature;for(const auto& cover:covers)signature+=cover.key+"|"+cover.title+"|"+cover.localPath+"\n";
    if(signature!=coverViewport_){coverViewport_=signature;metadata_->RequestCovers(covers);}
    for(auto& event:metadata_->PollEvents()){
        if(event.type==Metadata::Event::Type::CoverReady){
            const auto* image=event.bytes.empty()?nullptr:sfImage_createFromMemory(event.bytes.data(),event.bytes.size());
            if(image){const auto size=sfImage_getSize(image);if(size.x<=2048&&size.y<=2048){Texture texture(sfTexture_createFromImage(image,nullptr));if(texture)covers_[event.key]=std::move(texture);}sfImage_destroy(const_cast<sfImage*>(image));}
        }else if(event.type==Metadata::Event::Type::Error)notice_="ONLINE: "+event.message;
        else matched_.clear();
    }
    while(covers_.size()>60){auto victim=covers_.begin();if(victim->first==std::to_string(selected_.id)&&covers_.size()>1)++victim;covers_.erase(victim);}
}
void Application::Update(){
    if(index_){snapshot_=index_->GetSnapshot();if(snapshot_->revision!=seenRevision_){seenRevision_=snapshot_->revision;coverViewport_.clear();}
        if(!snapshot_->error.empty()&&snapshot_->error!=libraryError_){libraryError_=snapshot_->error;notice_="LIBRARY: "+snapshot_->error;}}
    if(directoryJob_.valid()&&directoryJob_.wait_for(std::chrono::seconds(0))==std::future_status::ready){directory_=directoryJob_.get();
        directory_.files.erase(std::remove_if(directory_.files.begin(),directory_.files.end(),[this](const Storage::File& file){
            if(file.link)return true;if(file.directory)return false;if(browserMode_==BrowserMode::Root)return true;
            return browserMode_==BrowserMode::Artwork?!Ends(file.path,".png"):!RomFile(file.path);
        }),directory_.files.end());ui_.ClearFocus();}
    if(inspecting_.valid()&&inspecting_.wait_for(std::chrono::seconds(0))==std::future_status::ready){
        try{auto result=inspecting_.get();if(archiveCancel_->load())throw Process::Cancelled();members_=std::move(result.members);
            const auto count=std::count_if(members_.begin(),members_.end(),[](const Archive::MemberInfo& member){return member.available;});
            if(count==1){const auto item=std::find_if(members_.begin(),members_.end(),[](const Archive::MemberInfo& member){return member.available;});BeginLoad(archivePath_,item->name,pendingCopy_);}
            else if(!count)Error("No playable games", "This archive contains no supported cartridge. Check the unavailable entries or choose another file.",session_.Active()?Page::Paused:Page::Library);
        }catch(const Process::Cancelled&){if(page_==Page::Archive)SetPage(session_.Active()?Page::Paused:Page::Library);}
        catch(const std::exception& error){retry_=[this]{OpenGame(archivePath_);};Error("Archive could not be opened",error.what(),session_.Active()?Page::Paused:Page::Library);}
    }
    if(loading_.valid()&&loading_.wait_for(std::chrono::seconds(0))==std::future_status::ready){
        try{auto next=loading_.get();if(loadCancel_->load())throw Process::Cancelled();
            auto recordPrevious=CaptureEnd("switched");
            session_.Start(std::move(next));recordPrevious();activeCopy_=pendingCopy_;activeLocator_={pendingPath_,pendingMember_};recorded_=false;
            if(activeCopy_){index_->RecordLoaded(activeCopy_,session_.Rom());index_->RecordSession(activeCopy_,0,"started");}
            else index_->RecordExternal(activeLocator_,session_.Rom(),0,"started");
            auto host=settings_;if(options_.noAudio)host.audioEnabled=false;AudioOutput::ApplySettings(host);
            SetPage(Page::Library);neutralResume_=false;nextFrame_=Clock::now();SetTitle();
        }catch(const Process::Cancelled&){if(page_==Page::Loading)SetPage(session_.Active()?Page::Paused:Page::Library);}
        catch(const std::exception& error){retry_=[this]{BeginLoad(pendingPath_,pendingMember_,pendingCopy_);};Error("Game could not be started",error.what(),session_.Active()?Page::Paused:Page::Library);}
    }
    UpdateMetadata();
}

void Application::Header(const std::string& title){
    ui_.Rect({0,0,640,41},Ui::Panel);
    if(branding_)ui_.Image(branding_.get(),{12,6,150,28},{60,90,210,39});
    else ui_.Text("DRIVONIX",12,9,20,Ui::Green);
    ui_.Text(title,180,11,16,Ui::White,440);
    ui_.Rect({0,39,213,2},Ui::Red);ui_.Rect({213,39,214,2},Ui::Green);ui_.Rect({427,39,213,2},Ui::Blue);
}
void Application::Footer(const std::string& text){ui_.Rect({0,337,640,23},Ui::Panel);ui_.Text(text,12,342,11,Ui::Muted,616);}
void Application::DrawLibrary(){
    Header("");
    ui_.Button("all","ALL",{181,8,38,23},true,query_.favoritesOnly||query_.recentOnly?Ui::Tone::Quiet:Ui::Tone::Accent);
    ui_.Button("favorites","FAV",{225,8,40,23},true,query_.favoritesOnly?Ui::Tone::Accent:Ui::Tone::Quiet);
    ui_.Button("recent","RECENT",{271,8,58,23},true,query_.recentOnly?Ui::Tone::Accent:Ui::Tone::Quiet);
    ui_.Button("open","OPEN",{336,8,51,23});
    ui_.Button("settings","SETTINGS",{492,8,82,23});ui_.Button("quit","QUIT",{581,8,48,23},true,Ui::Tone::Danger);
    ui_.Button("search",query_.search.empty()?"SEARCH GAMES...":query_.search,{12,47,312,23});
    ui_.Button("filters","FILTERS",{332,47,82,23});ui_.Button("addroot","ADD FOLDER",{422,47,110,23});
    const bool scanning=snapshot_&&snapshot_->progress.scanning;
    ui_.Button(scanning?"cancelscan":"scan",scanning?"CANCEL":"SCAN",{540,47,88,23});
    if(!snapshot_||!snapshot_->ready){ui_.Text("READING YOUR LIBRARY...",190,166,18,Ui::Green);}
    else if(snapshot_->games.empty()){
        ui_.Rect({80,98,480,182},Ui::Panel,Ui::Border);
        ui_.Text(settings_.libraryRoots.empty()?"YOUR GAMES LIVE HERE":"NO MATCHING GAMES",148,122,20,Ui::Green,390);
        ui_.Wrap(settings_.libraryRoots.empty()?"Add your ROM folder to build a library. ZIP and 7z collections stay compressed.":"Try clearing the search or filters. A missing folder remains in the library until a complete scan confirms its contents.",{113,159,416,58},14,Ui::Muted);
        ui_.Button("addrootempty","CHOOSE A FOLDER",{190,232,260,28},true,Ui::Tone::Accent);
    }else{
        for(std::size_t i=0;i<snapshot_->games.size()&&i<12;++i){const auto& game=snapshot_->games[i];
            const auto found=covers_.find(std::to_string(game.id));bool available=false;
            for(const auto& variant:game.variants)for(const auto& copy:variant.copies)available=available||copy.available;
            ui_.Card("game:"+std::to_string(game.id),game.metadata.title.empty()?game.title:game.metadata.title,
                {14+float(i%6)*103,79+float(i/6)*118,96,112},found==covers_.end()?nullptr:found->second.get(),game.favorite,available);
        }
    }
    ui_.Button("previous","< PREV",{12,318,74,18},query_.offset>0,Ui::Tone::Quiet,11);
    ui_.Button("next","NEXT >",{93,318,74,18},snapshot_&&query_.offset+12<snapshot_->totalGames,Ui::Tone::Quiet,11);
    const auto count=snapshot_?snapshot_->totalGames:0;
    ui_.Text(std::to_string(count)+" GAMES  /  "+std::to_string(snapshot_?snapshot_->totalVariants:0)+" VARIANTS",179,321,11,Ui::Muted,272);
    if(scanning)ui_.Text("SCANNING "+std::to_string(snapshot_->progress.inspected),464,321,11,Ui::Green,164);
    else ui_.Text(settings_.onlineMetadata?"ONLINE ART":"OFFLINE",505,321,11,Ui::Blue,124);
    std::string status="A / ENTER: OPEN   B / ESC: BACK   Y: SEARCH   LB/RB: PAGE";
    if(Starts(ui_.Focus(),"game:")&&snapshot_){try{const auto id=std::stoll(ui_.Focus().substr(5));for(const auto& game:snapshot_->games)if(game.id==id){status=game.metadata.title.empty()?game.title:game.metadata.title;break;}}catch(...){} }
    if(!notice_.empty())status=notice_;Footer(status);
}
void Application::DrawDetails(){
    Header("CHOOSE A VARIANT");
    ui_.Text(selected_.metadata.title.empty()?selected_.title:selected_.metadata.title,14,48,18,Ui::Green,610);
    const auto cover=covers_.find(std::to_string(selected_.id));
    ui_.Rect({16,78,146,192},Ui::Panel,Ui::Border);
    if(cover!=covers_.end())ui_.Image(cover->second.get(),{20,82,138,184});
    else{ui_.Text("DRIVONIX",35,157,17,Ui::Muted);ui_.Text("NO COVER",43,181,12,Ui::Muted);}
    const auto& meta=selected_.metadata;
    ui_.Text(meta.year?std::to_string(meta.year):"YEAR UNKNOWN",18,277,12,Ui::Muted,145);
    ui_.Text(meta.genre.empty()?"GENRE UNKNOWN":meta.genre,18,295,12,Ui::Muted,145);
    for(std::size_t i=listOffset_;i<selected_.variants.size()&&i<listOffset_+9;++i){const auto& variant=selected_.variants[i];
        bool available=false;for(const auto& copy:variant.copies)available=available||copy.available;
        ui_.Button("variant:"+std::to_string(i),variant.title,{177,78+float(i-listOffset_)*25,450,23},true,available?Ui::Tone::Normal:Ui::Tone::Quiet,12);
    }
    ui_.Button("varprev","<",{179,306,35,22},listOffset_>0);ui_.Button("varnext",">",{219,306,35,22},listOffset_+9<selected_.variants.size());
    ui_.Button("favorite",selected_.favorite?"UNFAVORITE":"FAVORITE",{264,306,103,22});
    ui_.Button("metadata","DETAILS",{375,306,84,22});ui_.Button("localart","COVER",{467,306,74,22});
    ui_.Button("back","BACK",{549,306,78,22});
    Footer(std::to_string(selected_.variants.size())+" VARIANTS  |  "+TimeText(selected_.playSeconds)+" PLAYED  |  LB/RB: PAGE");
}
void Application::DrawCopies(){
    Header("CHOOSE A COPY");
    if(variant_>=selected_.variants.size()){SetPage(Page::Details);return;}
    const auto& variant=selected_.variants[variant_];ui_.Text(variant.title,14,47,16,Ui::Green,612);
    for(std::size_t i=listOffset_;i<variant.copies.size()&&i<listOffset_+4;++i){const auto& copy=variant.copies[i];const float y=76+float(i-listOffset_)*55;
        ui_.Button("copy:"+std::to_string(i),"",{14,y,612,51},true,copy.available?Ui::Tone::Normal:Ui::Tone::Quiet);
        ui_.Text((copy.id==variant.preferredCopy?"* ":"")+(copy.locator.member.empty()?std::string("ROM: "):std::string("ARCHIVE: "))+copy.locator.path,23,y+5,12,copy.available?Ui::White:Ui::Muted,592);
        ui_.Text(copy.available?(copy.duplicateCandidate?"DUPLICATE CANDIDATE - SEPARATE SAVE":"AVAILABLE - SEPARATE SAVE"):(copy.error.empty()?copy.status:copy.error),23,y+23,11,copy.available?Ui::Green:Ui::Red,592);
    }
    ui_.Button("copyprev","< PREV",{14,305,86,25},listOffset_>0);
    ui_.Button("copynext","NEXT >",{108,305,86,25},listOffset_+4<variant.copies.size());ui_.Button("back","BACK",{540,305,86,25});
    Footer("The chosen file keeps its existing battery save. No copies or saves are merged.");
}
void Application::DrawArchive(){
    Header("SELECT A GAME FROM ARCHIVE");ui_.Text(Storage::Filename(archivePath_),14,47,15,Ui::Green,610);
    if(inspecting_.valid())ui_.Text("READING ARCHIVE CONTENTS...",140,165,18,Ui::Muted);
    for(std::size_t i=archiveOffset_;i<members_.size()&&i<archiveOffset_+9;++i){const auto& member=members_[i];
        ui_.Button("member:"+std::to_string(i),member.name,{14,75+float(i-archiveOffset_)*25,612,23},true,member.available?Ui::Tone::Normal:Ui::Tone::Quiet,12);
    }
    ui_.Button("arcprev","< PREV",{14,306,85,24},archiveOffset_>0);ui_.Button("arcnext","NEXT >",{107,306,85,24},archiveOffset_+9<members_.size());
    ui_.Button("back","CANCEL",{528,306,98,24});Footer("A / ENTER: SELECT   B / ESC: CANCEL   LB/RB: PAGE");
}
void Application::DrawBrowser(){
    Header(browserMode_==BrowserMode::Root?"ADD A ROM FOLDER":browserMode_==BrowserMode::Artwork?"CHOOSE A PNG COVER":"OPEN A GAME");
    ui_.Button("browserpath",browserPath_,{14,47,468,24},true,Ui::Tone::Quiet,12);
    ui_.Button("browserup","UP",{490,47,62,24});ui_.Button("browserhome","HOME",{560,47,66,24});
    if(directoryJob_.valid())ui_.Text("READING FOLDER...",220,170,16,Ui::Muted);
    else if(!directory_.error.empty())ui_.Wrap(directory_.error,{30,110,580,120},15,Ui::Red);
    for(std::size_t i=browserOffset_;i<directory_.files.size()&&i<browserOffset_+10;++i){const auto& file=directory_.files[i];
        ui_.Button("file:"+std::to_string(i),(file.directory?"[+] ":"    ")+Storage::Filename(file.path),{14,79+float(i-browserOffset_)*21,612,19},true,file.directory?Ui::Tone::Accent:Ui::Tone::Normal,12);
    }
    ui_.Button("browserprev","<",{14,303,36,26},browserOffset_>0);ui_.Button("browsernext",">",{57,303,36,26},browserOffset_+10<directory_.files.size());
    if(browserMode_==BrowserMode::Root)ui_.Button("usefolder","USE THIS FOLDER",{102,303,190,26},!directoryJob_.valid()&&directory_.error.empty(),Ui::Tone::Accent);
    else if(browserMode_==BrowserMode::Game)ui_.Button("nativeopen","NATIVE PICKER",{102,303,152,26});
#ifdef _WIN32
    ui_.Button("drives","DRIVES",{309,303,82,26});
#endif
    ui_.Button("browsercancel","CANCEL",{530,303,96,26});Footer("A: OPEN FOLDER / SELECT FILE   B: BACK   LB/RB: PAGE");
}
void Application::DrawPaused(){
    Header("PAUSED");ui_.Rect({118,62,404,258},Ui::Panel,Ui::Border,2);
    ui_.Text(session_.Rom().title,141,76,16,Ui::Green,360);
    const char* ids[]={"resume","restart","settings","open","library","quit"};
    const char* labels[]={"RESUME GAME","RESTART CARTRIDGE","SETTINGS","OPEN ANOTHER GAME","RETURN TO LIBRARY","QUIT DRIVONIX"};
    for(unsigned i=0;i<6;++i)ui_.Button(ids[i],labels[i],{151,107+float(i)*33,338,28},true,i==0?Ui::Tone::Accent:i==5?Ui::Tone::Danger:Ui::Tone::Normal);
    Footer("R3 / ESC / F1: MENU   A: CONFIRM   B: RESUME   Battery saves are automatic");
}
void Application::Draw(const Input::UiActions& actions){
    sfRenderWindow_clear(window_,Ui::Background);
    const auto size=sfRenderWindow_getSize(window_);sfView* pixels=sfView_createFromRect({0,0,float(size.x),float(size.y)});
    sfRenderWindow_setView(window_,pixels);sfView_destroy(pixels);
    if(session_.Active())CRT::Draw({0,0,float(size.x),float(size.y)},settings_.integerFit,settings_.scale);
    const bool gameOnly=session_.Active()&&!session_.Paused()&&page_==Page::Library;
    if(!gameOnly){
        sfRectangleShape* shade=sfRectangleShape_create();sfRectangleShape_setSize(shade,{float(size.x),float(size.y)});
        sfRectangleShape_setFillColor(shade,session_.Active()?sfColor{1,3,9,205}:Ui::Background);sfRenderWindow_drawRectangleShape(window_,shade,nullptr);sfRectangleShape_destroy(shade);
    }
    ui_.Begin(window_,actions);const auto logical=ui_.ToLogical(pointerPixels_);
    ui_.Begin(window_,actions,{logical.x,logical.y,mouseMoved_,mouseClicked_});
    if(gameOnly){if(session_.Booting()){ui_.Rect({132,312,376,35},sfColor{4,8,15,220});ui_.Button("skipbios","A / ENTER: SKIP BOOT",{140,316,360,27},true,Ui::Tone::Accent);}}
    else if(neutralResume_){ui_.Text("RELEASE CONTROLS TO RESUME",159,165,18,Ui::Green);}
    else switch(page_){
    case Page::Library:DrawLibrary();break;
    case Page::Details:DrawDetails();break;
    case Page::Copies:DrawCopies();break;
    case Page::Archive:DrawArchive();break;
    case Page::Browser:DrawBrowser();break;
    case Page::Paused:DrawPaused();break;
    case Page::Settings:case Page::Unsaved:DrawSettings();break;
    case Page::Bindings:case Page::Capture:case Page::Conflict:DrawBindings();break;
    case Page::Text:DrawTextEditor();break;
    case Page::Filters:DrawFilters();break;
    case Page::Match:DrawMatch();break;
    case Page::Error:DrawError();break;
    case Page::About:
        Header("ABOUT DRIVONIX");ui_.Text("DRIVONIX",200,69,32,Ui::Green);ui_.Text(version_,200,109,14,Ui::Muted,410);
        ui_.Wrap("Genesis / Mega Drive emulator\nOriginal emulator & BIOS: Julien Magnin\nOriginal unit tests: Okba Hamidi\n\nArtwork and game metadata: Libretro\nPixel fonts: GNU Unifont (OFL 1.1)\nLibrary storage: SQLite\n\nGame ROMs are not included.",{58,146,528,160},14,Ui::White);
        ui_.Button("back","BACK",{490,305,128,26});Footer("DrivOnix - the original retro identity, with a controller-friendly frontend");break;
    case Page::Loading:
        Header("PREPARING GAME");ui_.Text(Storage::Filename(pendingPath_),50,112,18,Ui::Green,540);
        ui_.Wrap("Reading cartridge and validating its battery save. Your current game stays paused until preparation succeeds.",{60,159,520,80},15,Ui::Muted);
        ui_.Button("back","CANCEL",{220,271,200,28});Footer("Preparation can be cancelled without replacing the active game.");break;
    case Page::SaveFailure:
        Header("BATTERY SAVE NEEDS ATTENTION");ui_.Wrap(errorMessage_,{35,70,570,110},14,Ui::Red);
        if(confirmDiscard_){ui_.Wrap("Exit without saving? Progress since the last successful battery save will be lost.",{50,188,540,57},16,Ui::White);
            ui_.Button("discardconfirmed","EXIT WITHOUT SAVING",{50,264,300,32},true,Ui::Tone::Danger);ui_.Button("discardcancel","CANCEL",{375,264,210,32});}
        else{ui_.Button("retrysave","RETRY",{55,220,155,32},true,Ui::Tone::Accent);ui_.Button("keepsession","KEEP PLAYING",{228,220,180,32});
            ui_.Button("discard","QUIT WITHOUT SAVING",{168,273,300,30},true,Ui::Tone::Danger);}
        Footer("The game remains paused and its pending battery memory is retained.");break;
    }
    const auto action=ui_.Finish();sfRenderWindow_display(window_);
    try{
        if(!action.empty())Dispatch(action);
        else if(actions.back&&!gameOnly&&page_!=Page::Capture)Back();
        else if(actions.search&&page_==Page::Library&&!session_.Active())Dispatch("search");
        else if(actions.pageDown){if(page_==Page::Library&&!session_.Active())Dispatch("next");else if(page_==Page::Details)Dispatch("varnext");else if(page_==Page::Archive)Dispatch("arcnext");else if(page_==Page::Browser)Dispatch("browsernext");}
        else if(actions.pageUp){if(page_==Page::Library&&!session_.Active())Dispatch("previous");else if(page_==Page::Details)Dispatch("varprev");else if(page_==Page::Archive)Dispatch("arcprev");else if(page_==Page::Browser)Dispatch("browserprev");}
        else if(actions.menu&&!gameOnly){if(page_==Page::Paused)ResumeGame();else Back();}
    }catch(const std::exception& error){Error("Action could not be completed",error.what(),session_.Active()?Page::Paused:Page::Library);}
}

void Application::Back(){
    if(DispatchSettings("back"))return;
    switch(page_){
    case Page::Library:if(session_.Active()&&session_.Paused())ResumeGame();break;
    case Page::Details:SetPage(Page::Library);RefreshQuery(false);break;
    case Page::Copies:listOffset_=(variant_/9)*9;SetPage(Page::Details);ui_.Focus("variant:"+std::to_string(variant_));break;
    case Page::Archive:if(archiveCancel_)archiveCancel_->store(true);SetPage(session_.Active()?Page::Paused:Page::Library);break;
    case Page::Browser:{const auto parent=Storage::Parent(browserPath_);if(parent!=browserPath_&&browserPath_!="DRIVES")ReadDirectory(parent);else SetPage(browserBack_);break;}
    case Page::Paused:ResumeGame();break;
    case Page::About:SetPage(Page::Settings);break;
    case Page::Loading:if(loadCancel_)loadCancel_->store(true);SetPage(session_.Active()?Page::Paused:Page::Library);break;
    case Page::SaveFailure:confirmDiscard_=false;SetPage(Page::Paused);break;
    default:break;
    }
}
void Application::Dispatch(const std::string& action){
    if(DispatchSettings(action))return;
    if(action=="back"){Back();return;}
    if(action=="quit"){RequestQuit();return;}
    if(action=="all"||action=="favorites"||action=="recent"){
        query_.favoritesOnly=action=="favorites";query_.recentOnly=action=="recent";
        query_.sort=action=="recent"?Library::Sort::LastPlayed:Library::Sort::Title;RefreshQuery();return;
    }
    if(action=="search"){EditText("SEARCH GAMES",query_.search,[this](std::string value){query_.search=std::move(value);RefreshQuery();},Page::Library);return;}
    if(action=="filters"){SetPage(Page::Filters);return;}
    if(action=="next"){if(snapshot_&&query_.offset+12<snapshot_->totalGames){query_.offset+=12;RefreshQuery(false);ui_.ClearFocus();}return;}
    if(action=="previous"){if(query_.offset>=12){query_.offset-=12;RefreshQuery(false);ui_.ClearFocus();}return;}
    if(action=="scan"){index_->Scan();notice_.clear();return;}
    if(action=="cancelscan"){index_->CancelScan();notice_="SCAN CANCELLED - EXISTING ENTRIES RETAINED";return;}
    if(action=="addroot"||action=="addrootempty"){draft_=settings_;OpenBrowser(BrowserMode::Root);return;}
    if(action=="open"){OpenBrowser(BrowserMode::Game);return;}
    if(action=="settings"){
        settingsBack_=session_.Active()?Page::Paused:Page::Library;session_.Pause();draft_=settings_;settingsTab_=0;SetPage(Page::Settings);SetTitle();return;
    }
    if(Starts(action,"game:")){SelectGame(std::stoll(action.substr(5)));return;}
    if(Starts(action,"variant:")){
        variant_=std::size_t(std::stoull(action.substr(8)));if(variant_>=selected_.variants.size())return;
        const auto& variant=selected_.variants[variant_];index_->SetPreferredVariant(selected_.id,variant.id);
        if(variant.copies.size()==1){Launch(variant.copies.front());return;}
        std::size_t choice=0;for(std::size_t i=0;i<variant.copies.size();++i)if(variant.copies[i].id==variant.preferredCopy)choice=i;
        listOffset_=(choice/4)*4;SetPage(Page::Copies);ui_.Focus("copy:"+std::to_string(choice));return;
    }
    if(Starts(action,"copy:")){const auto i=std::size_t(std::stoull(action.substr(5)));if(variant_<selected_.variants.size()&&i<selected_.variants[variant_].copies.size())Launch(selected_.variants[variant_].copies[i]);return;}
    if(action=="varnext"){if(listOffset_+9<selected_.variants.size())listOffset_+=9;ui_.ClearFocus();return;}
    if(action=="varprev"){listOffset_=listOffset_>=9?listOffset_-9:0;ui_.ClearFocus();return;}
    if(action=="copynext"){if(variant_<selected_.variants.size()&&listOffset_+4<selected_.variants[variant_].copies.size())listOffset_+=4;ui_.ClearFocus();return;}
    if(action=="copyprev"){listOffset_=listOffset_>=4?listOffset_-4:0;ui_.ClearFocus();return;}
    if(action=="favorite"){selected_.favorite=!selected_.favorite;index_->SetFavorite(selected_.id,selected_.favorite);return;}
    if(action=="metadata"){
        const auto catalog=metadata_->Snapshot();matches_=catalog?catalog->Search(selected_.title,50):std::vector<Metadata::Game>();listOffset_=0;SetPage(Page::Match);return;
    }
    if(action=="localart"){OpenBrowser(BrowserMode::Artwork);return;}
    if(Starts(action,"member:")){
        const auto i=std::size_t(std::stoull(action.substr(7)));if(i>=members_.size())return;
        if(!members_[i].available){Error("Unsupported variant",members_[i].diagnostic.empty()?members_[i].status:members_[i].diagnostic,Page::Archive);return;}
        BeginLoad(archivePath_,members_[i].name,pendingCopy_);return;
    }
    if(action=="arcnext"){if(archiveOffset_+9<members_.size())archiveOffset_+=9;ui_.ClearFocus();return;}
    if(action=="arcprev"){archiveOffset_=archiveOffset_>=9?archiveOffset_-9:0;ui_.ClearFocus();return;}
    if(action=="browserpath"){EditText("FOLDER PATH",browserPath_,[this](std::string value){ReadDirectory(Storage::Absolute(value));},Page::Browser);return;}
    if(action=="browserup"){ReadDirectory(Storage::Parent(browserPath_));return;}
    if(action=="browserhome"){ReadDirectory(Home());return;}
    if(action=="browsercancel"){SetPage(browserBack_);return;}
    if(action=="browsernext"){if(browserOffset_+10<directory_.files.size())browserOffset_+=10;ui_.ClearFocus();return;}
    if(action=="browserprev"){browserOffset_=browserOffset_>=10?browserOffset_-10:0;ui_.ClearFocus();return;}
    if(action=="drives"){
#ifdef _WIN32
        directory_.files.clear();directory_.error.clear();browserOffset_=0;browserPath_="DRIVES";
        const auto drives=GetLogicalDrives();for(unsigned i=0;i<26;++i)if(drives&(DWORD(1)<<i)){Storage::File file;file.path=std::string(1,char('A'+i))+":/";file.directory=true;directory_.files.push_back(std::move(file));}
#endif
        return;
    }
    if(action=="usefolder"){
        const auto path=Storage::Absolute(browserPath_);
        if(!Storage::Stat(path).directory)throw std::runtime_error("Select a folder first");
        const auto exists=std::find_if(draft_.libraryRoots.begin(),draft_.libraryRoots.end(),[&](const Settings::LibraryRoot& root){return Storage::Absolute(root.path)==path;});
        if(exists==draft_.libraryRoots.end()){
            std::vector<byte> key(path.begin(),path.end());const auto id="root_"+RomInfo::Sha256(key).substr(0,16);
            draft_.libraryRoots.push_back({id,path,true,true});
        }else exists->enabled=true;
        if(browserBack_==Page::Settings){SetPage(Page::Settings);settingsTab_=3;}
        else{SetPage(Page::Library);SaveSettings();RefreshQuery();}
        return;
    }
    if(Starts(action,"file:")){
        const auto i=std::size_t(std::stoull(action.substr(5)));if(i>=directory_.files.size())return;const auto file=directory_.files[i];
        if(file.directory){ReadDirectory(file.path);return;}
        if(browserMode_==BrowserMode::Artwork){
            const auto bytes=Storage::ReadBytes(file.path,8*1024*1024);Metadata::ValidatePng(bytes);
            const auto target=Storage::Join(Storage::Join(dataDir_,"artwork"),RomInfo::Sha256(bytes)+".png");Storage::AtomicWrite(target,bytes);
            selected_.metadata.artworkPath=target;index_->ApplyMetadata(selected_.id,selected_.metadata,true);coverViewport_.clear();SetPage(Page::Details);return;
        }
        OpenGame(file.path);return;
    }
    if(action=="nativeopen"){const auto path=Platform::SelectRom();if(!path.empty())OpenGame(path);return;}
    if(action=="resume"){ResumeGame();return;}
    if(action=="skipbios"){session_.SkipBios();router_.SuppressUntilNeutral();return;}
    if(action=="restart"){
        try{auto recordPrevious=CaptureEnd("restarted");session_.Restart();recordPrevious();recorded_=false;SetPage(Page::Library);neutralResume_=true;session_.Pause();nextFrame_=Clock::now();SetTitle();}
        catch(const std::exception& error){retry_=[this]{Dispatch("restart");};Error("Cartridge could not restart",error.what(),Page::Paused);}return;
    }
    if(action=="library"){ReturnLibrary();return;}
    if(action=="retrysave"){if(pendingStop_=="quit")RequestQuit();else ReturnLibrary();return;}
    if(action=="keepsession"){SetPage(Page::Paused);return;}
    if(action=="discard"){confirmDiscard_=true;ui_.ClearFocus();router_.SuppressUntilNeutral();return;}
    if(action=="discardcancel"){confirmDiscard_=false;ui_.ClearFocus();router_.SuppressUntilNeutral();return;}
    if(action=="discardconfirmed"){RecordEnd("discarded");discardExit_=true;quit_=true;return;}
}

}
int RunApplication(const Options& options){return Detail::Application(options).Run();}
}
