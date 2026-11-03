#include "application_internal.hpp"

// Native-resolution pages share the application's existing action IDs and
// state transitions. The original Retronix drawers remain in application.cpp.
namespace Frontend { namespace Detail {
namespace {
std::string Title(const Library::Game& game){return game.metadata.title.empty()?game.title:game.metadata.title;}
std::string SizeText(uint64_t bytes){
    if(bytes>=1024*1024){std::ostringstream out;out.precision(1);out<<std::fixed<<double(bytes)/(1024*1024)<<" MiB";return out.str();}
    return std::to_string((bytes+1023)/1024)+" KiB";
}
void Surface(Ui::Canvas& ui,sfFloatRect area){const auto& p=ui.Colors();ui.Rounded(area,p.panel,p.border);}
void Section(Ui::Canvas& ui,const std::string& text,float x,float y,float width){const auto& p=ui.Colors();ui.Text(text,x,y,13,p.muted,width);}
void PageButtons(Ui::Canvas& ui,const std::string& previous,const std::string& next,
                 bool before,bool after,const std::string& back="back",const std::string& label="Back"){
    const auto& c=ui.Context();const float y=c.height-78;
    ui.Button(previous,"Previous",{24,y,112,38},before,Ui::Tone::Quiet,14);
    ui.Button(next,"Next",{146,y,94,38},after,Ui::Tone::Quiet,14);
    ui.Button(back,label,{c.width-152,y,128,38},true,Ui::Tone::Normal,14);
}
}

void Application::DrawThemedLibrary(){
    const auto& c=ui_.Context();const auto& p=ui_.Colors();const auto layout=Ui::LibraryLayout(c);
    const bool modern=c.theme==Ui::ThemeId::Modern;
    const bool sidebar=modern&&!c.compact;
    const bool scanning=snapshot_&&snapshot_->progress.scanning;
    const auto allTone=query_.favoritesOnly||query_.recentOnly?Ui::Tone::Quiet:Ui::Tone::Accent;
    if(sidebar){
        ui_.Rect({0,0,208,c.height-20},p.panel);ui_.Rect({207,64,1,c.height-84},p.border);
        Header(query_.favoritesOnly?"Favorites":query_.recentOnly?"Recently played":"Your library");
        ui_.BeginScroll("library.navigation",{12,78,184,c.height-104},456);
        Section(ui_,"Library",24,80,160);
        ui_.Button("all","All games",{20,104,168,38},true,allTone,16);
        ui_.Button("favorites","Favorites",{20,150,168,38},true,query_.favoritesOnly?Ui::Tone::Accent:Ui::Tone::Quiet,16);
        ui_.Button("recent","Recently played",{20,196,168,38},true,query_.recentOnly?Ui::Tone::Accent:Ui::Tone::Quiet,16);
        ui_.Rect({28,250,152,1},p.border);Section(ui_,"Collection",24,267,160);
        ui_.Button("open","Open game",{20,291,168,38},true,Ui::Tone::Normal,16);
        ui_.Button("addroot","Add folder",{20,337,168,38},true,Ui::Tone::Normal,16);
        ui_.Button(scanning?"cancelscan":"scan",scanning?"Cancel scan":"Scan library",{20,383,168,38},true,Ui::Tone::Quiet,16);
        ui_.Button("settings","Settings",{20,443,168,38},true,Ui::Tone::Quiet,16);
        ui_.Button("quit","Quit",{20,489,168,38},true,Ui::Tone::Danger,16);
        ui_.EndScroll();
        ui_.Button("search",query_.search.empty()?"Search your games...":query_.search,{layout.grid.left,82,layout.grid.width-124,38},true,Ui::Tone::Quiet,16);
        ui_.Button("filters","Filters",{c.width-136,82,112,38},true,Ui::Tone::Normal,16);
    }else if(c.compact){
        Header("");
        const char* ids[]={"all","favorites","recent","settings","quit"};
        const char* labels[]={"All","Favorites","Recent","Settings","Quit"};
        const float widths[]={52,80,72,100,66};float x=214;
        for(unsigned i=0;i<5;++i){
            const auto tone=i==0?allTone:i==1?(query_.favoritesOnly?Ui::Tone::Accent:Ui::Tone::Quiet):i==2?(query_.recentOnly?Ui::Tone::Accent:Ui::Tone::Quiet):i==4?Ui::Tone::Danger:Ui::Tone::Quiet;
            ui_.Button(ids[i],labels[i],{x,14,widths[i],36},true,tone,14);x+=widths[i]+8;
        }
        const float searchY=modern?80:70,toolY=modern?128:110;
        ui_.Button("search",query_.search.empty()?"Search your games...":query_.search,{24,searchY,c.width-48,34},true,Ui::Tone::Quiet,16);
        const float width=(c.width-78)/4;
        ui_.Button("filters","Filters",{24,toolY,width,38},true,Ui::Tone::Normal,15);
        ui_.Button("open","Open game",{34+width,toolY,width,38},true,Ui::Tone::Normal,15);
        ui_.Button("addroot","Add folder",{44+2*width,toolY,width,38},true,Ui::Tone::Normal,15);
        ui_.Button(scanning?"cancelscan":"scan",scanning?"Cancel scan":"Scan library",{54+3*width,toolY,width,38},true,Ui::Tone::Quiet,15);
    }else{
        Header("");
        ui_.Button("all","All games",{232,13,94,38},true,allTone,16);
        ui_.Button("favorites","Favorites",{336,13,100,38},true,query_.favoritesOnly?Ui::Tone::Accent:Ui::Tone::Quiet,16);
        ui_.Button("recent","Recent",{446,13,88,38},true,query_.recentOnly?Ui::Tone::Accent:Ui::Tone::Quiet,16);
        ui_.Button("open","Open game",{544,13,110,38},true,Ui::Tone::Normal,16);
        ui_.Button("settings","Settings",{c.width-220,13,110,38},true,Ui::Tone::Quiet,16);
        ui_.Button("quit","Quit",{c.width-100,13,76,38},true,Ui::Tone::Danger,16);
        const float searchWidth=c.width-456;
        ui_.Button("search",query_.search.empty()?"Search your games...":query_.search,{24,94,searchWidth,40},true,Ui::Tone::Quiet,16);
        ui_.Button("filters","Filters",{searchWidth+36,94,110,40},true,Ui::Tone::Normal,16);
        ui_.Button("addroot","Add folder",{searchWidth+158,94,144,40},true,Ui::Tone::Normal,16);
        ui_.Button(scanning?"cancelscan":"scan",scanning?"Cancel scan":"Scan library",{c.width-142,94,118,40},true,Ui::Tone::Quiet,16);
    }
    if(snapshot_&&!snapshot_->ready&&!snapshot_->error.empty()){
        Surface(ui_,layout.grid);ui_.BeginScroll("library.error",layout.grid,174);
        ui_.Text("Library needs attention",layout.grid.left+20,layout.grid.top+16,24,p.red,layout.grid.width-40);
        ui_.Wrap(snapshot_->error,{layout.grid.left+20,layout.grid.top+54,layout.grid.width-40,64},16,p.text);
        ui_.Button("retryindex","Retry library",{layout.grid.left+20,layout.grid.top+126,std::min(240.f,layout.grid.width-40),38},true,Ui::Tone::Accent,16);ui_.EndScroll();
    }else if(!snapshot_||!snapshot_->ready){
        Surface(ui_,layout.grid);ui_.Text("Reading your library...",layout.grid.left+22,layout.grid.top+24,22,p.muted,layout.grid.width-44);
    }else if(snapshot_->games.empty()){
        Surface(ui_,layout.grid);ui_.BeginScroll("library.empty",layout.grid,190);
        ui_.Text(settings_.libraryRoots.empty()?"Your games live here":"No matching games",layout.grid.left+22,layout.grid.top+18,26,p.text,layout.grid.width-44);
        ui_.Wrap(settings_.libraryRoots.empty()?"Add your ROM folder to start a collection. ZIP and 7z archives stay compressed.":"Try changing your search or filters. You can also add another ROM folder.",{layout.grid.left+22,layout.grid.top+60,layout.grid.width-44,72},16,p.muted);
        ui_.Button("addrootempty","Choose a folder",{layout.grid.left+22,layout.grid.top+140,std::min(240.f,layout.grid.width-44),38},true,Ui::Tone::Accent,16);ui_.EndScroll();
    }else for(std::size_t i=0;i<snapshot_->games.size()&&i<layout.capacity;++i){
        const auto& game=snapshot_->games[i];const auto found=covers_.find(std::to_string(game.id));bool available=false;
        for(const auto& variant:game.variants)for(const auto& copy:variant.copies)available=available||copy.available;
        ui_.Card("game:"+std::to_string(game.id),Title(game),layout.Card(i),found==covers_.end()?nullptr:found->second.get(),game.favorite,available);
    }
    const float pagerY=c.height-52;ui_.Button("previous","Previous",{layout.grid.left,pagerY,100,30},query_.offset>0,Ui::Tone::Quiet,13);
    ui_.Button("next","Next",{layout.grid.left+110,pagerY,82,30},snapshot_&&query_.offset+layout.capacity<snapshot_->totalGames,Ui::Tone::Quiet,13);
    const auto count=snapshot_?snapshot_->totalGames:0;
    std::string summary=std::to_string(count)+" games";
    if(snapshot_)summary+="  ·  "+std::to_string(snapshot_->totalVariants)+" variants";
    if(scanning)summary+="  ·  Scanning "+std::to_string(snapshot_->progress.inspected);
    else summary+=settings_.onlineMetadata?"  ·  Online artwork":"  ·  Offline";
    ui_.Text(summary,layout.grid.left+208,pagerY+7,13,p.muted,layout.grid.width-208);
    std::string status="Enter / A: Open   Esc / B: Back   Y: Search   LB / RB: Page";
    if(Starts(ui_.Focus(),"game:")&&snapshot_)for(const auto& game:snapshot_->games)if(ui_.Focus()=="game:"+std::to_string(game.id)){status=Title(game);break;}
    Footer(notice_.empty()?status:notice_);
}

void Application::DrawThemedDetails(){
    const auto& c=ui_.Context();const auto& p=ui_.Colors();Header("Game details");
    ui_.Text(Title(selected_),24,80,26,p.text,c.width-48);
    ui_.Text(std::to_string(selected_.variants.size())+" variants  ·  "+TimeText(selected_.playSeconds)+" played",24,114,13,p.muted,c.width-48);
    const float top=146,height=c.height-246,coverWidth=c.compact?142.f:std::min(240.f,c.width*0.23f),left=coverWidth+48;
    const auto cover=covers_.find(std::to_string(selected_.id));const auto& meta=selected_.metadata;
    Surface(ui_,{24,top,coverWidth,height});
    const float imageHeight=(coverWidth-20)*1.34f;
    ui_.BeginScroll("details.metadata",{24,top,coverWidth,height},imageHeight+244);
    if(cover!=covers_.end())ui_.Image(cover->second.get(),{34,top+10,coverWidth-20,imageHeight});
    else{ui_.Rounded({34,top+10,coverWidth-20,imageHeight},p.input);ui_.Text("No cover",40,top+imageHeight/2,16,p.muted,coverWidth-32);}
    float y=top+imageHeight+24;
    ui_.Text(meta.year?std::to_string(meta.year):"Year unknown",36,y,16,p.text,coverWidth-24);y+=28;
    ui_.Wrap(meta.genre.empty()?"Genre unknown":meta.genre,{36,y,coverWidth-24,46},14,p.muted);y+=52;
    ui_.Wrap(meta.publisher.empty()?"Publisher unknown":meta.publisher,{36,y,coverWidth-24,48},14,p.muted);y+=54;
    if(!meta.developer.empty()){ui_.Wrap(meta.developer,{36,y,coverWidth-24,46},14,p.muted);y+=52;}
    if(meta.players)ui_.Text(std::to_string(meta.players)+(meta.players==1?" player":" players"),36,y,14,p.muted,coverWidth-24);
    ui_.EndScroll();
    const auto capacity=Capacity(Ui::ListKind::Variants);const std::size_t end=std::min(selected_.variants.size(),listOffset_+capacity);
    ui_.BeginScroll("details.variants",{left,top,c.width-left-24,height},float(end-std::min(end,listOffset_))*52);
    for(std::size_t i=listOffset_;i<end;++i){const auto& variant=selected_.variants[i];bool available=false;for(const auto& copy:variant.copies)available=available||copy.available;
        const float rowY=top+float(i-listOffset_)*52;
        ui_.Button("variant:"+std::to_string(i),variant.title,{left,rowY,c.width-left-24,42},true,available?Ui::Tone::Normal:Ui::Tone::Quiet,16);
    }
    ui_.EndScroll();
    const float actionY=c.height-78,gap=10,buttonWidth=(c.width-48-112-3*gap)/4;
    ui_.Button("varprev","<",{24,actionY,46,38},listOffset_>0,Ui::Tone::Quiet,16);
    ui_.Button("varnext",">",{80,actionY,46,38},listOffset_+capacity<selected_.variants.size(),Ui::Tone::Quiet,16);
    const float actionX=136;
    ui_.Button("favorite",selected_.favorite?"Unfavorite":"Favorite",{actionX,actionY,buttonWidth,38},true,Ui::Tone::Normal,14);
    ui_.Button("metadata","Game info",{actionX+buttonWidth+gap,actionY,buttonWidth,38},true,Ui::Tone::Normal,14);
    ui_.Button("localart","Set cover",{actionX+2*(buttonWidth+gap),actionY,buttonWidth,38},true,Ui::Tone::Normal,14);
    ui_.Button("back","Back",{actionX+3*(buttonWidth+gap),actionY,buttonWidth,38},true,Ui::Tone::Quiet,14);
    Footer("Choose a variant to play. Multiple source files keep their own battery saves.   LB / RB: Page");
}

void Application::DrawThemedCopies(){
    if(variant_>=selected_.variants.size()){SetPage(Page::Details);return;}
    const auto& c=ui_.Context();const auto& p=ui_.Colors();const auto& variant=selected_.variants[variant_];Header("Choose a source");
    ui_.Text(variant.title,24,80,24,p.text,c.width-48);
    Section(ui_,"Choose the file to launch. Each source keeps its existing battery save.",24,114,c.width-48);
    const auto capacity=Capacity(Ui::ListKind::Copies);const auto end=std::min(variant.copies.size(),listOffset_+capacity);
    const sfFloatRect viewport{24,146,c.width-48,c.height-246};const float rowHeight=std::min(112.f,viewport.height);
    ui_.BeginScroll("copies.list",viewport,float(end-std::min(end,listOffset_))*(rowHeight+12));
    for(std::size_t i=listOffset_;i<end;++i){const auto& copy=variant.copies[i];const float y=viewport.top+float(i-listOffset_)*(rowHeight+12);
        ui_.Button("copy:"+std::to_string(i),"",{24,y,c.width-48,rowHeight},true,copy.available?Ui::Tone::Normal:Ui::Tone::Quiet,16);
        ui_.Text((copy.id==variant.preferredCopy?"Preferred · ":"")+(copy.locator.member.empty()?std::string("Cartridge file"):std::string("Archive member")),38,y+10,16,p.text,c.width-76);
        ui_.Text(copy.locator.path,38,y+36,13,p.muted,c.width-76);
        if(!copy.locator.member.empty())ui_.Text(copy.locator.member,38,y+55,13,p.muted,c.width-76);
        const std::string status=copy.available?(copy.duplicateCandidate?"Duplicate candidate · Separate save":"Available · Separate save"):(copy.error.empty()?copy.status:copy.error);
        ui_.Text(status,38,y+rowHeight-25,13,copy.available?p.green:p.red,c.width-76);
    }
    ui_.EndScroll();PageButtons(ui_,"copyprev","copynext",listOffset_>0,listOffset_+capacity<variant.copies.size());
    Footer("Select an unavailable source to see its diagnostic. Copies and saves are kept separate.");
}

void Application::DrawThemedArchive(){
    const auto& c=ui_.Context();const auto& p=ui_.Colors();Header("Choose a game from archive");
    ui_.Text(Storage::Filename(archivePath_),24,80,24,p.text,c.width-48);
    Section(ui_,std::to_string(members_.size())+" cartridge entries · Exact member names are preserved",24,114,c.width-48);
    const auto capacity=Capacity(Ui::ListKind::Archive);const auto end=std::min(members_.size(),archiveOffset_+capacity);
    const sfFloatRect viewport{24,146,c.width-48,c.height-246};
    if(inspecting_.valid())ui_.Text("Reading archive contents...",38,162,20,p.muted,c.width-76);
    ui_.BeginScroll("archive.members",viewport,float(end-std::min(end,archiveOffset_))*76);
    for(std::size_t i=archiveOffset_;i<end;++i){const auto& member=members_[i];const float y=viewport.top+float(i-archiveOffset_)*76;
        ui_.Button("member:"+std::to_string(i),"",{24,y,c.width-48,66},true,member.available?Ui::Tone::Normal:Ui::Tone::Quiet,16);
        ui_.Text(member.name,38,y+10,16,member.available?p.text:p.muted,c.width-76);
        ui_.Text(member.available?SizeText(member.size):member.diagnostic.empty()?member.status:member.diagnostic,38,y+37,13,member.available?p.muted:p.red,c.width-76);
    }
    ui_.EndScroll();PageButtons(ui_,"arcprev","arcnext",archiveOffset_>0,archiveOffset_+capacity<members_.size(),"back","Cancel");
    Footer("Enter / A: Select   Esc / B: Cancel   Unavailable entries explain why they cannot be loaded.");
}

void Application::DrawThemedBrowser(){
    const auto& c=ui_.Context();const auto& p=ui_.Colors();Header(browserMode_==BrowserMode::Root?"Add a ROM folder":browserMode_==BrowserMode::Artwork?"Choose a cover":"Open a game");
    ui_.Button("browserpath",browserPath_,{24,82,c.width-250,38},true,Ui::Tone::Quiet,15);
    ui_.Button("browserup","Up",{c.width-214,82,78,38},true,Ui::Tone::Normal,16);
    ui_.Button("browserhome","Home",{c.width-126,82,102,38},true,Ui::Tone::Normal,16);
    const auto capacity=Capacity(Ui::ListKind::Browser);const auto end=std::min(directory_.files.size(),browserOffset_+capacity);
    const sfFloatRect viewport{24,140,c.width-48,c.height-240};
    ui_.BeginScroll("browser.files",viewport,std::max(float(end-std::min(end,browserOffset_))*48,120.f));
    if(directoryJob_.valid())ui_.Text("Reading folder...",38,156,20,p.muted,c.width-76);
    else if(!directory_.error.empty())ui_.Wrap(directory_.error,{38,156,c.width-76,100},16,p.red);
    else if(directory_.files.empty())ui_.Text("No matching files in this folder",38,156,18,p.muted,c.width-76);
    for(std::size_t i=browserOffset_;i<end;++i){const auto& file=directory_.files[i];
        ui_.Button("file:"+std::to_string(i),(file.directory?"Folder  ·  ":"")+Storage::Filename(file.path),{24,140+float(i-browserOffset_)*48,c.width-48,38},true,file.directory?Ui::Tone::Accent:Ui::Tone::Normal,16);
    }
    ui_.EndScroll();const float y=c.height-78;
    ui_.Button("browserprev","<",{24,y,38,38},browserOffset_>0,Ui::Tone::Quiet,16);
    ui_.Button("browsernext",">",{72,y,38,38},browserOffset_+capacity<directory_.files.size(),Ui::Tone::Quiet,16);
    if(browserMode_==BrowserMode::Root)ui_.Button("usefolder","Use this folder",{122,y,184,38},!directoryJob_.valid()&&directory_.error.empty(),Ui::Tone::Accent,16);
    else if(browserMode_==BrowserMode::Game)ui_.Button("nativeopen","System picker",{122,y,168,38},true,Ui::Tone::Normal,16);
#ifdef _WIN32
    ui_.Button("drives","Drives",{320,y,96,38},true,Ui::Tone::Quiet,16);
#endif
    ui_.Button("browsercancel","Cancel",{c.width-146,y,122,38},true,Ui::Tone::Quiet,16);
    Footer(browserMode_==BrowserMode::Artwork?"Select a local PNG cover. Your source artwork stays in place.":"Enter / A: Open folder or select file   Esc / B: Back   LB / RB: Page");
}

void Application::DrawThemedPaused(){
    const auto& c=ui_.Context();const auto& p=ui_.Colors();Header("Paused");
    const float width=std::min(620.f,c.width-48),left=(c.width-width)/2;
    const sfFloatRect area{left,86,width,c.height-126};Surface(ui_,area);
    ui_.BeginScroll("paused.actions",{left+16,102,width-32,c.height-158},374);
    ui_.Text(session_.Rom().title,left+24,106,24,p.text,width-48);
    Section(ui_,"Your game and battery memory are retained.",left+24,140,width-48);
    const char* ids[]={"resume","restart","settings","open","library","quit"};
    const char* labels[]={"Resume game","Restart cartridge","Settings","Open another game","Return to library","Quit DrivOnix"};
    for(unsigned i=0;i<6;++i)ui_.Button(ids[i],labels[i],{left+24,178+float(i)*48,width-48,38},true,i==0?Ui::Tone::Accent:i==5?Ui::Tone::Danger:Ui::Tone::Normal,16);
    ui_.EndScroll();Footer("Esc / F1 / R3: Menu   Enter / A: Confirm   B: Resume   Battery saves are automatic");
}

void Application::DrawThemedAbout(){
    const auto& c=ui_.Context();const auto& p=ui_.Colors();Header("About DrivOnix");
    const float width=std::min(880.f,c.width-48),left=(c.width-width)/2;Surface(ui_,{left,86,width,c.height-186});
    ui_.BeginScroll("about.credits",{left+20,106,width-40,c.height-226},510);
    ui_.Wordmark({left+24,110,std::min(360.f,width-48),60});
    ui_.Text(version_,left+24,187,14,p.muted,width-48);
    ui_.Text("Genesis / Mega Drive emulator",left+24,222,24,p.text,width-48);
    ui_.Wrap("Original emulator and BIOS: Julien Magnin\nOriginal unit tests: Okba Hamidi\n\nArtwork and game metadata: Libretro\nPixel fonts: GNU Unifont · OFL 1.1\nInterface fonts: Roboto · Apache 2.0\nLibrary storage: SQLite · Public domain\n\nGame ROMs are not included.",{left+24,266,width-48,320},16,p.muted);
    ui_.EndScroll();ui_.Button("back","Back",{c.width-152,c.height-78,128,38},true,Ui::Tone::Normal,16);
    Footer("DrivOnix · Genesis / Mega Drive · The original identity, with your choice of interface");
}

void Application::DrawThemedLoading(){
    const auto& c=ui_.Context();const auto& p=ui_.Colors();Header("Preparing game");
    const float width=std::min(800.f,c.width-48),left=(c.width-width)/2;const sfFloatRect area{left,86,width,c.height-186};Surface(ui_,area);
    ui_.BeginScroll("loading.status",{left+20,106,width-40,c.height-226},260);
    ui_.Rect({left+24,112,40,4},p.red);ui_.Rect({left+70,112,40,4},p.green);ui_.Rect({left+116,112,40,4},p.blue);
    ui_.Text(Storage::Filename(pendingPath_),left+24,140,24,p.text,width-48);
    if(!pendingMember_.empty())ui_.Text(pendingMember_,left+24,180,14,p.muted,width-48);
    ui_.Wrap("Reading the cartridge and validating its battery save. Your current game stays paused until preparation succeeds.",{left+24,218,width-48,124},16,p.muted);
    ui_.EndScroll();ui_.Button("back","Cancel preparation",{(c.width-240)/2,c.height-78,240,38},true,Ui::Tone::Quiet,16);
    Footer("Preparation can be cancelled without replacing the active game.");
}

void Application::DrawThemedSaveFailure(){
    const auto& c=ui_.Context();const auto& p=ui_.Colors();Header("Battery save needs attention");
    const float width=std::min(850.f,c.width-48),left=(c.width-width)/2;Surface(ui_,{left,86,width,c.height-186});
    // Keep recovery choices fixed. Giving the first action a place below a
    // long diagnostic would auto-scroll its cause out of view on first focus.
    ui_.BeginScroll("savefailure.content",{left+20,106,width-40,c.height-226},260);
    ui_.Text(confirmDiscard_?"Exit without saving?":"Your game is still safe in memory",left+24,110,24,confirmDiscard_?p.red:p.text,width-48);
    if(confirmDiscard_){
        ui_.Wrap("Progress since the last successful battery save will be lost. You can cancel and retry saving instead.",{left+24,156,width-48,72},16,p.text);
        ui_.Wrap(errorMessage_,{left+24,242,width-48,106},16,p.red);
    }else{
        ui_.Wrap(errorMessage_,{left+24,156,width-48,106},16,p.red);
        ui_.Wrap("The game remains paused. Retry the save, or keep playing while you resolve the file problem.",{left+24,276,width-48,72},16,p.muted);
    }
    ui_.EndScroll();const float actionY=c.height-78;
    if(confirmDiscard_){const float button=(width-12)/2;
        ui_.Button("discardconfirmed","Exit without saving",{left,actionY,button,38},true,Ui::Tone::Danger,16);
        ui_.Button("discardcancel","Cancel",{left+button+12,actionY,button,38},true,Ui::Tone::Normal,16);
    }else{const float button=(width-24)/3;
        ui_.Button("retrysave","Retry save",{left,actionY,button,38},true,Ui::Tone::Accent,16);
        ui_.Button("keepsession","Keep playing",{left+button+12,actionY,button,38},true,Ui::Tone::Normal,16);
        ui_.Button("discard","Quit without saving",{left+2*(button+12),actionY,button,38},true,Ui::Tone::Danger,16);
    }
    Footer("Pending battery memory is retained until you explicitly confirm exiting without saving.");
}
}}

