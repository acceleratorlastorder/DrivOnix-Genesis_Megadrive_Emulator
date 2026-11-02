#include "../cathoderaytube/crt.hpp"
#include "application_internal.hpp"
#include <limits>

namespace Frontend { namespace Detail {
namespace {
std::string On(bool value){return value?"ON":"OFF";}
std::string Upper(std::string value){for(char& c:value)if(c>='a'&&c<='z')c=char(c-'a'+'A');return value;}
unsigned Integer(const std::string& value,unsigned minimum,unsigned maximum) {
    if(value.empty())throw std::invalid_argument("Enter a number from "+std::to_string(minimum)+" through "+std::to_string(maximum));
    unsigned result=0;
    for(char c:value){if(c<'0'||c>'9'||result>maximum/10)throw std::invalid_argument("The number is outside the supported range");result=result*10+unsigned(c-'0');if(result>maximum)throw std::invalid_argument("The number is outside the supported range");}
    if(result<minimum)throw std::invalid_argument("The number is below "+std::to_string(minimum));
    return result;
}
std::size_t SuffixIndex(const std::string& action,const std::string& prefix) {
    return Integer(action.substr(prefix.size()),0,1000000);
}
std::string Joined(const std::set<std::string>& values) {
    std::string result;
    for(const auto& value:values){if(value.empty()||Starts(value,"axis_")||Starts(value,"button_"))continue;if(!result.empty())result+="  ";result+=value;}
    return result.empty()?"NEUTRAL":result;
}
std::string DeviceLabel(int index,const Input::Frame& frame) {
    if(index<0)return "DISABLED";
    const auto& device=frame.devices[unsigned(index)];
    return std::to_string(index)+(device.connected?"  "+device.name:"  NOT CONNECTED");
}
std::string NextValue(const std::set<std::string>& available,const std::string& current) {
    std::vector<std::string> choices{""};
    for(const auto& value:available)if(!value.empty())choices.push_back(value);
    const auto found=std::find(choices.begin(),choices.end(),current);
    return found==choices.end()||found+1==choices.end()?"":*(found+1);
}
std::set<std::string> FilterValues(const std::shared_ptr<const Library::Snapshot>& snapshot,bool genres) {
    std::set<std::string> values;
    if(snapshot){const auto& facets=genres?snapshot->genres:snapshot->regions;
        if(!facets.empty()){values.insert(facets.begin(),facets.end());return values;}}
    if(snapshot)for(const auto& game:snapshot->games){
        if(genres){if(!game.metadata.genre.empty())values.insert(game.metadata.genre);}
        else{if(!game.metadata.region.empty())values.insert(game.metadata.region);for(const auto& variant:game.variants)if(!variant.region.empty())values.insert(variant.region);}
    }
    return values;
}
}

void Application::DrawSettings() {
    Header(page_==Page::Unsaved?"UNSAVED SETTINGS":"SETTINGS");
    if(page_==Page::Unsaved){
        ui_.Rect({58,78,524,210},Ui::Panel,Ui::Border,2);
        ui_.Text("KEEP YOUR CHANGES?",157,95,22,Ui::Green);
        ui_.Wrap("These changes have not been saved or applied. Choose what to do before leaving settings.",{88,134,466,51},15,Ui::White);
        ui_.Button("s.unsaved.save","SAVE & RETURN",{90,202,218,31},document_!=nullptr,Ui::Tone::Accent);
        ui_.Button("s.unsaved.discard","DISCARD CHANGES",{330,202,218,31},true,Ui::Tone::Danger);
        ui_.Button("s.unsaved.stay","KEEP EDITING",{203,247,232,29});
        Footer("The existing INI stays unchanged until a save succeeds.");return;
    }
    const char* tabs[]={"DISPLAY","AUDIO","INPUT","FOLDERS","ONLINE","ABOUT"};
    for(unsigned tab=0;tab<6;++tab)ui_.Button("s.tab."+std::to_string(tab),tabs[tab],
        {16+float(tab)*103,48,96,26},true,settingsTab_==tab?Ui::Tone::Accent:Ui::Tone::Quiet,12);
    ui_.Rect({16,84,608,209},Ui::Panel,Ui::Border);
    switch(settingsTab_){
    case 0:
        ui_.Button("s.fullscreen","FULLSCREEN: "+On(draft_.fullscreen),{30,98,277,28});
        ui_.Button("s.fit",draft_.integerFit?"GAME SCALE: INTEGER FIT":"GAME SCALE: FIXED",{331,98,277,28});
        ui_.Text("FIXED SCALE  "+std::to_string(draft_.scale)+"X",33,142,14,Ui::White);
        ui_.Button("s.scale.down","-",{222,136,37,27},draft_.scale>1);
        ui_.Button("s.scale.up","+",{270,136,37,27},draft_.scale<8);
        ui_.Button("s.bios","BIOS BOOT: "+On(draft_.showBios),{331,136,277,28});
        ui_.Button("s.width","WINDOW WIDTH: "+std::to_string(draft_.windowWidth),{30,176,277,28});
        ui_.Button("s.height","WINDOW HEIGHT: "+std::to_string(draft_.windowHeight),{331,176,277,28});
        ui_.Wrap(draft_.integerFit?"Integer fit uses the largest whole-pixel game image that fits the window.":"Fixed scale keeps your chosen game pixel size. The image fits inside the available window.",{32,218,573,38},13,Ui::Muted);
        ui_.Button("s.size.preset","NEXT WINDOW SIZE PRESET",{150,260,340,24},true,Ui::Tone::Quiet,12);break;
    case 1:
        ui_.Button("s.audio","HOST AUDIO: "+On(draft_.audioEnabled),{40,106,560,32});
        ui_.Text("VOLUME",42,153,16,Ui::White);ui_.Text(std::to_string(draft_.volume)+"%",513,153,16,Ui::Green,90);
        ui_.Rect({40,185,560,10},Ui::Background,Ui::Border);
        ui_.Rect({41,186,558.f*float(draft_.volume)/100.f,8},Ui::Green);
        ui_.Button("s.volume.down","- 5",{40,211,140,30},draft_.volume>0);
        ui_.Button("s.volume.up","+ 5",{460,211,140,30},draft_.volume<100);
        ui_.Wrap(options_.noAudio?"This launch uses --no-audio. The file preference is saved, but playback stays muted for this launch.":"Sound-chip execution continues even when host playback is disabled.",{42,256,554,31},12,Ui::Muted);break;
    case 2:
        ui_.Button("s.controllers","GAME CONTROLLERS: "+On(draft_.controllersEnabled),{30,97,322,26});
        ui_.Text("DEADZONE "+std::to_string(draft_.controllerDeadzone)+"%",366,102,12,Ui::White);
        ui_.Button("s.deadzone.down","-",{520,97,34,26},draft_.controllerDeadzone>1);
        ui_.Button("s.deadzone.up","+",{567,97,34,26},draft_.controllerDeadzone<95);
        for(unsigned player=0;player<2;++player){
            const auto index=player?draft_.controller2:draft_.controller1;const float y=137+float(player)*38;
            ui_.Text("P"+std::to_string(player+1)+"  "+DeviceLabel(index,frame_),31,y+5,12,Ui::White,446);
            ui_.Button("s.device."+std::to_string(player)+".down","<",{494,y,45,27},index>-1);
            ui_.Button("s.device."+std::to_string(player)+".up",">",{555,y,45,27},index<7);
        }
        ui_.Button("s.bindings","KEYBOARD & CONTROLLER BINDINGS",{67,218,505,29},true,Ui::Tone::Accent);
        ui_.Wrap("Menu controls stay available when game input is disabled. R3 / Escape / F1 opens the menu; F11 toggles fullscreen.",{32,259,573,30},12,Ui::Muted);break;
    case 3:{
        ui_.Text(std::to_string(draft_.libraryRoots.size())+" LIBRARY FOLDERS",30,92,12,Ui::Muted);
        ui_.Button("s.root.add","ADD FOLDER",{424,90,183,23},draft_.libraryRoots.size()<256,Ui::Tone::Accent,12);
        if(listOffset_>=draft_.libraryRoots.size())listOffset_=draft_.libraryRoots.empty()?0:((draft_.libraryRoots.size()-1)/5)*5;
        for(std::size_t i=listOffset_;i<draft_.libraryRoots.size()&&i<listOffset_+5;++i){
            const auto& root=draft_.libraryRoots[i];const float y=121+float(i-listOffset_)*29;
            const auto prefix="s.root."+std::to_string(i)+".";
            ui_.Button(prefix+"path",root.path,{29,y,340,25},true,Ui::Tone::Quiet,11);
            ui_.Button(prefix+"enabled",root.enabled?"ON":"OFF",{377,y,58,25},true,root.enabled?Ui::Tone::Accent:Ui::Tone::Quiet,11);
            ui_.Button(prefix+"recursive",root.recursive?"SUB ON":"SUB OFF",{443,y,77,25},true,Ui::Tone::Quiet,10);
            ui_.Button(prefix+"remove","REMOVE",{528,y,80,25},true,Ui::Tone::Danger,11);
        }
        if(draft_.libraryRoots.empty())ui_.Wrap("Choose a folder containing your ROMs or archives. Adding a folder does not move or change its files.",{90,151,460,74},16,Ui::Muted);
        ui_.Button("s.root.previous","< PREV",{30,269,90,19},listOffset_>0,Ui::Tone::Quiet,10);
        ui_.Button("s.root.next","NEXT >",{129,269,90,19},listOffset_+5<draft_.libraryRoots.size(),Ui::Tone::Quiet,10);
        ui_.Text("SUB = SCAN SUBFOLDERS",341,272,10,Ui::Muted,266);break;
    }
    case 4:
        ui_.Button("s.online","ONLINE INFO & ARTWORK: "+On(draft_.onlineMetadata),{34,100,572,30});
        ui_.Wrap("Local games work offline. Online downloads supply game information and artwork; your ROM files stay local.",{37,143,566,44},14,Ui::Muted);
        ui_.Text(metadata_&&metadata_->Snapshot()?"CATALOG AVAILABLE":"CATALOG NOT AVAILABLE YET",37,199,12,Ui::Green,566);
        ui_.Button("s.metadata.refresh","REFRESH CATALOG",{34,229,277,27},settings_.onlineMetadata&&metadata_!=nullptr);
        ui_.Button("s.metadata.clear","CLEAR ARTWORK CACHE",{329,229,277,27},metadata_!=nullptr);
        ui_.Button("s.open.cache","OPEN CACHE FOLDER",{177,265,286,21},true,Ui::Tone::Quiet,11);break;
    default:
        ui_.Text("DRIVONIX "+version_,32,98,18,Ui::Green,570);
        ui_.Text("SETTINGS FILE",32,134,11,Ui::Muted);
        ui_.Text(options_.configPath,32,152,12,Ui::White,570);
        ui_.Text(configDefaultsOnce_?"TEMPORARY DEFAULTS - ORIGINAL FILE UNCHANGED":"FILE AND UI SHARE THE SAME SETTINGS",32,178,11,configDefaultsOnce_?Ui::Red:Ui::Muted,570);
        ui_.Button("s.open.config","OPEN SETTINGS FOLDER",{31,209,277,27});
        ui_.Button("s.open.data","OPEN DATA FOLDER",{331,209,277,27});
        if(configDefaultsOnce_)ui_.Button("s.reload.config","RELOAD REPAIRED SETTINGS",{110,253,420,28},true,Ui::Tone::Accent);
        else ui_.Button("s.about.details","CREDITS & LICENSE INFORMATION",{110,253,420,28});
        break;
    }
    bool dirty=true;try{dirty=DraftDirty();}catch(...){}
    ui_.Button("s.save","SAVE & APPLY",{17,305,247,27},document_!=nullptr,Ui::Tone::Accent);
    ui_.Button("s.cancel",dirty?"CANCEL / BACK":"BACK",{385,305,239,27});
    ui_.Text(dirty?"UNSAVED":"SAVED",278,313,10,dirty?Ui::Green:Ui::Muted,98);
    Footer(configDefaultsOnce_?"Temporary defaults: repair and reload your original settings file to save changes.":
        settingsTab_==3?"Removing a folder changes the index settings; it never deletes games.":"A / ENTER: CHANGE   B / ESC: BACK   Changes remain a draft until Save & Apply.");
}

void Application::DrawBindings() {
    Header("PLAYER "+std::to_string(bindingPlayer_+1)+" GAME BINDINGS");
    if(page_==Page::Capture){
        ui_.Rect({55,76,530,221},Ui::Panel,Ui::Border,2);
        ui_.Text("ASSIGN "+Upper(Input::ActionName(bindingAction_)),88,96,22,Ui::Green,465);
        ui_.Wrap(captureStatus_.empty()?"Release controls, then press a key, button or move an axis.":captureStatus_,{89,145,461,71},16,Ui::White);
        ui_.Text("Escape / F1 / R3 cancels. Reserved controls cannot drive the game.",88,231,11,Ui::Muted,466);
        ui_.Button("capture.cancel","CANCEL",{205,259,230,28});Footer("Only fresh input is captured; the game remains paused.");return;
    }
    if(page_==Page::Conflict){
        ui_.Rect({47,76,546,221},Ui::Panel,Ui::Border,2);
        const auto conflicts=Input::Conflicts(draft_.bindings[bindingPlayer_],bindingDevice_,bindingAction_,captured_);
        std::string names;for(auto action:conflicts){if(!names.empty())names+=", ";names+=Upper(Input::ActionName(action));}
        ui_.Text("THIS CONTROL IS ALREADY USED",79,94,20,Ui::Green,480);
        ui_.Wrap(captured_+" is assigned to "+names+". "+(conflicts.size()==1?"Swap the two actions, or keep the original bindings.":"Several actions use this control. Clear those bindings first, or choose another control."),{80,138,481,76},15,Ui::White);
        ui_.Button("capture.swap","SWAP ACTIONS",{79,248,221,30},conflicts.size()==1,Ui::Tone::Accent);
        ui_.Button("capture.cancel","CANCEL",{324,248,235,30});
        Footer("No existing game binding changes until the conflict is resolved.");return;
    }
    ui_.Button("bind.player.0","PLAYER 1",{16,48,112,26},true,bindingPlayer_==0?Ui::Tone::Accent:Ui::Tone::Quiet);
    ui_.Button("bind.player.1","PLAYER 2",{136,48,112,26},true,bindingPlayer_==1?Ui::Tone::Accent:Ui::Tone::Quiet);
    ui_.Button("bind.keyboard","KEYBOARD",{290,48,157,26},true,bindingDevice_==Input::Device::Keyboard?Ui::Tone::Accent:Ui::Tone::Quiet);
    ui_.Button("bind.controller","CONTROLLER",{455,48,169,26},true,bindingDevice_==Input::Device::Controller?Ui::Tone::Accent:Ui::Tone::Quiet);
    const unsigned layout[]={Input::Up,Input::Down,Input::Left,Input::Right,Input::Start,Input::Mode,Input::A,Input::B,Input::C,Input::X,Input::Y,Input::Z};
    const auto& profile=draft_.bindings[bindingPlayer_];const auto& list=bindingDevice_==Input::Device::Keyboard?profile.keyboard:profile.controller;
    for(unsigned i=0;i<12;++i){const auto action=layout[i];
        ui_.Button("bind.action."+std::to_string(action),Upper(Input::ActionName(action))+"  :  "+Upper(list[action]),
            {16+float(i/6)*312,88+float(i%6)*28,255,25},true,Ui::Tone::Normal,12);
        ui_.Button("bind.clear."+std::to_string(action),"CLR",
            {280+float(i/6)*312,88+float(i%6)*28,32,25},true,Ui::Tone::Quiet,10);
    }
    if(bindingDevice_==Input::Device::Keyboard)ui_.Text("LIVE KEYS: "+Joined(frame_.keys),18,265,12,Ui::Green,604);
    else {
        const auto index=bindingPlayer_?draft_.controller2:draft_.controller1;
        ui_.Text("LIVE: "+DeviceLabel(index,frame_),18,262,12,index>=0&&frame_.devices[unsigned(index)].connected?Ui::Green:Ui::Red,604);
        if(index>=0){const auto& state=frame_.devices[unsigned(index)];
            ui_.Text("L "+std::to_string(state.leftX)+"/"+std::to_string(state.leftY)+"  R "+std::to_string(state.rightX)+"/"+std::to_string(state.rightY)+
                "  LT/RT "+std::to_string(state.leftTrigger)+"/"+std::to_string(state.rightTrigger)+"  "+Joined(Input::ControllerSymbols(state,draft_.controllerDeadzone)),18,282,10,Ui::Muted,604);
        }
    }
    ui_.Button("bind.reset","RESTORE THIS TAB",{17,307,235,26});
    ui_.Button("bind.done","DONE",{385,307,239,26},true,Ui::Tone::Accent);
    Footer("Choose an action to rebind it; CLR unbinds it. Keyboard and controller stay independent.");
}

void Application::FinishCapture(const Input::CaptureResult& result) {
    if(std::any_of(frame_.devices.begin(),frame_.devices.end(),[](const Controller::State& device){
        return device.connected&&(device.buttons&Controller::RightThumb);
    })){
        capture_.Cancel();notice_="BINDING CANCELLED";SetPage(Page::Bindings);
        ui_.Focus("bind.action."+std::to_string(bindingAction_));return;
    }
    if(result.status==Input::CaptureStatus::WaitingForNeutral){captureStatus_="Release the controls that opened this screen.";return;}
    if(result.status==Input::CaptureStatus::Listening){captureStatus_="Press a fresh key or button, or move a stick/trigger deliberately.";return;}
    if(result.status==Input::CaptureStatus::Cancelled||result.status==Input::CaptureStatus::Disconnected){
        notice_=result.status==Input::CaptureStatus::Disconnected?"CONTROLLER DISCONNECTED - BINDING UNCHANGED":"BINDING CANCELLED";
        SetPage(Page::Bindings);ui_.Focus("bind.action."+std::to_string(bindingAction_));return;
    }
    if(result.status!=Input::CaptureStatus::Captured)return;
    captured_=result.binding;
    try {
        const auto conflicts=Input::Conflicts(draft_.bindings[bindingPlayer_],bindingDevice_,bindingAction_,captured_);
        if(!conflicts.empty()){SetPage(Page::Conflict);return;}
        std::string error;
        if(!Input::Assign(draft_.bindings[bindingPlayer_],bindingDevice_,bindingAction_,captured_,false,&error))throw std::runtime_error(error);
        notice_="BINDING UPDATED";SetPage(Page::Bindings);ui_.Focus("bind.action."+std::to_string(bindingAction_));
    }catch(const std::exception& error){Error("Binding could not be assigned",error.what(),Page::Bindings);}
}

void Application::DrawTextEditor() {
    Header(textTitle_);
    ui_.Rect({18,57,604,53},Ui::Panel,Ui::Green);
    auto visible=Ui::Canvas::Decode(textValue_);if(visible.size()>60){visible.erase(visible.begin(),visible.end()-60);visible.insert(visible.begin(),0x2026);}
    ui_.Wrap(Ui::Canvas::Encode(visible)+"|",{28,65,584,40},14,Ui::White);
    const char* letters[]={"qwertyuiop","asdfghjkl'","zxcvbnm/\\?"};
    const char* symbols[]={"1234567890","-_./\\:;#\"'","!@$%&()[]+"};
    for(unsigned row=0;row<3;++row){
        std::string keys=textSymbols_?symbols[row]:letters[row];if(textUpper_)keys=Upper(keys);
        for(unsigned column=0;column<keys.size();++column){const auto key=static_cast<unsigned char>(keys[column]);
            ui_.Button("text.key."+std::to_string(key),std::string(1,char(key)),{18+float(column)*61,123+float(row)*31,55,27},true,Ui::Tone::Normal,15);
        }
    }
    ui_.Button("text.space","SPACE",{18,222,192,27});
    ui_.Button("text.backspace","BACKSPACE",{220,222,192,27});
    ui_.Button("text.case",textUpper_?"LOWER":"UPPER",{422,222,94,27});
    ui_.Button("text.symbols",textSymbols_?"ABC":"123 / #",{526,222,96,27});
    ui_.Button("text.apply","APPLY",{18,268,292,31},true,Ui::Tone::Accent);
    ui_.Button("text.cancel","CANCEL",{330,268,292,31});
    ui_.Button("text.clear","CLEAR",{18,309,125,22},!textValue_.empty(),Ui::Tone::Quiet,11);
    ui_.Text("Physical keyboard and Unicode text entry are supported.",160,314,11,Ui::Muted,460);
    if(frame_.keys.count("enter"))ui_.Focus("text.apply");
    Footer("D-PAD: CHOOSE KEY   A: TYPE / ACTIVATE   B: CANCEL   ENTER: APPLY");
}

void Application::DrawFilters() {
    Header("LIBRARY FILTERS");
    ui_.Button("filter.region","REGION: "+(query_.region.empty()?std::string("ALL"):query_.region),{20,58,468,31});
    ui_.Button("filter.region.text","TYPE...",{500,58,120,31});
    ui_.Button("filter.genre","GENRE: "+(query_.genre.empty()?std::string("ALL"):query_.genre),{20,100,468,31});
    ui_.Button("filter.genre.text","TYPE...",{500,100,120,31});
    ui_.Button("filter.year","YEAR: "+(query_.year?std::to_string(query_.year):std::string("ALL")),{20,142,200,31});
    ui_.Button("filter.year.text","TYPE...",{230,142,80,31});
    const auto sort=query_.sort==Library::Sort::Title?"TITLE":query_.sort==Library::Sort::LastPlayed?"LAST PLAYED":"MOST PLAYED";
    ui_.Button("filter.sort",std::string("SORT: ")+sort,{330,142,290,31});
    ui_.Button("filter.favorites","FAVORITES ONLY: "+On(query_.favoritesOnly),{20,184,290,31});
    ui_.Button("filter.recent","RECENT ONLY: "+On(query_.recentOnly),{330,184,290,31});
    ui_.Button("filter.available",query_.includeUnavailable?"SHOW UNAVAILABLE: ON":"SHOW UNAVAILABLE: OFF",{20,226,600,31});
    ui_.Button("filter.clear","RESET FILTERS",{20,296,290,31});
    ui_.Button("filter.done","DONE",{330,296,290,31},true,Ui::Tone::Accent);
    Footer("Filters update your local library. Buttons cycle values from all indexed games.");
}

void Application::DrawMatch() {
    Header("MATCH GAME INFORMATION");
    ui_.Button("match.search","SEARCH CATALOG...",{17,49,331,28},metadata_&&metadata_->Snapshot()!=nullptr,Ui::Tone::Accent);
    ui_.Button("match.refresh","REFRESH",{359,49,124,28},metadata_&&settings_.onlineMetadata);
    ui_.Button("match.back","BACK",{494,49,130,28});
    ui_.Text(selected_.metadata.title.empty()?selected_.title:selected_.metadata.title,20,85,13,Ui::Green,600);
    if(!metadata_||!metadata_->Snapshot())ui_.Wrap(settings_.onlineMetadata?"The catalog is still loading. Refresh it or return to the library.":"No cached catalog is available. Enable online information in Settings to fetch it.",{70,150,500,92},16,Ui::Muted);
    else if(matches_.empty())ui_.Wrap("No matching catalog entries. Search another title, or keep the local game information.",{70,150,500,92},16,Ui::Muted);
    if(listOffset_>=matches_.size())listOffset_=matches_.empty()?0:((matches_.size()-1)/6)*6;
    for(std::size_t i=listOffset_;i<matches_.size()&&i<listOffset_+6;++i){const auto& match=matches_[i];
        const auto detail=(match.region.empty()?"":(" ["+match.region+"]"))+(match.year?" "+std::to_string(match.year):"");
        ui_.Button("match.choose."+std::to_string(i),match.title+detail,{18,108+float(i-listOffset_)*29,604,25},true,Ui::Tone::Normal,12);
    }
    ui_.Button("match.previous","< PREV",{18,301,105,27},listOffset_>0);
    ui_.Button("match.next","NEXT >",{134,301,105,27},listOffset_+6<matches_.size());
    ui_.Button("match.auto","USE AUTOMATIC MATCHING",{300,301,322,27});
    Footer("Choosing an entry saves a manual information override; ROMs and battery saves stay separate.");
}

void Application::DrawError() {
    Header(errorTitle_.empty()?"SOMETHING NEEDS ATTENTION":errorTitle_);
    ui_.Rect({18,57,604,172},Ui::Panel,Ui::Red);
    ui_.Wrap(errorMessage_,{32,69,575,150},13,Ui::White);
    if(!settingsError_.empty()&&!configDefaultsOnce_){
        ui_.Text("ORIGINAL SETTINGS FILE HAS NOT BEEN REPLACED",22,237,11,Ui::Muted,596);
        ui_.Button("error.reload","RETRY LOADING SETTINGS",{20,260,290,28},true,Ui::Tone::Accent);
        ui_.Button("error.openconfig","OPEN SETTINGS FOLDER",{330,260,290,28});
        ui_.Button("error.defaults","USE DEFAULTS ONCE",{20,299,290,28});
        ui_.Button("error.exit","EXIT",{330,299,290,28},true,Ui::Tone::Danger);
    } else {
        ui_.Button("error.retry","RETRY",{20,247,188,28},bool(retry_),Ui::Tone::Accent);
        ui_.Button("error.open","CHOOSE ANOTHER GAME",{225,247,395,28});
        ui_.Button("error.settings","SETTINGS",{20,296,188,28});
        const auto error=Lower(errorMessage_);const bool archiver=error.find("7-zip")!=std::string::npos||error.find("7zip")!=std::string::npos;
        if(archiver)ui_.Button("error.install","GET 7-ZIP",{225,296,188,28});
        else ui_.Button("error.back","RETURN",{225,296,188,28});
        ui_.Button("error.exit","EXIT",{430,296,190,28},true,Ui::Tone::Danger);
    }
    Footer("Your source ROMs remain unchanged. Fix the issue, retry, or choose another game.");
}

bool Application::DispatchSettings(const std::string& action) {
    if(action=="back"){
        switch(page_){
        case Page::Text:textFinish_={};SetPage(textBack_);return true;
        case Page::Settings:if(DraftDirty())SetPage(Page::Unsaved);else SetPage(settingsBack_);return true;
        case Page::Unsaved:if(pendingStop_=="settingsquit")pendingStop_.clear();SetPage(Page::Settings);return true;
        case Page::Bindings:SetPage(Page::Settings);return true;
        case Page::Capture:capture_.Cancel();SetPage(Page::Bindings);return true;
        case Page::Conflict:SetPage(Page::Bindings);return true;
        case Page::Filters:SetPage(Page::Library);return true;
        case Page::Match:listOffset_=(variant_/9)*9;SetPage(Page::Details);return true;
        case Page::Error:
            if(!settingsError_.empty()&&!configDefaultsOnce_)ui_.Focus("error.defaults");
            else SetPage(errorBack_);
            return true;
        default:return false;
        }
    }
    if(Starts(action,"s.")){
        if(Starts(action,"s.tab.")){settingsTab_=unsigned(SuffixIndex(action,"s.tab."));if(settingsTab_>5)settingsTab_=0;listOffset_=0;ui_.ClearFocus();}
        else if(action=="s.fullscreen")draft_.fullscreen=!draft_.fullscreen;
        else if(action=="s.fit")draft_.integerFit=!draft_.integerFit;
        else if(action=="s.bios")draft_.showBios=!draft_.showBios;
        else if(action=="s.scale.down")draft_.scale=std::max(1u,draft_.scale-1);
        else if(action=="s.scale.up")draft_.scale=std::min(8u,draft_.scale+1);
        else if(action=="s.width"||action=="s.height"){
            const bool width=action=="s.width";EditText(width?"WINDOW WIDTH":"WINDOW HEIGHT",std::to_string(width?draft_.windowWidth:draft_.windowHeight),[this,width](std::string value){
                auto next=draft_;const auto size=Integer(value,width?640:360,width?7680:4320);(width?next.windowWidth:next.windowHeight)=size;Settings::Validate(next);draft_=std::move(next);
            },Page::Settings);
        }else if(action=="s.size.preset"){
            const std::pair<unsigned,unsigned> presets[]={{640,360},{960,540},{1280,720},{1600,900},{1920,1080},{2560,1440},{3840,2160}};
            unsigned next=0;for(unsigned i=0;i<7;++i)if(draft_.windowWidth==presets[i].first&&draft_.windowHeight==presets[i].second)next=(i+1)%7;
            draft_.windowWidth=presets[next].first;draft_.windowHeight=presets[next].second;
        }else if(action=="s.audio")draft_.audioEnabled=!draft_.audioEnabled;
        else if(action=="s.volume.down")draft_.volume=draft_.volume>5?draft_.volume-5:0;
        else if(action=="s.volume.up")draft_.volume=std::min(100u,draft_.volume+5);
        else if(action=="s.controllers")draft_.controllersEnabled=!draft_.controllersEnabled;
        else if(action=="s.deadzone.down")draft_.controllerDeadzone=draft_.controllerDeadzone>5?draft_.controllerDeadzone-5:1;
        else if(action=="s.deadzone.up")draft_.controllerDeadzone=std::min(95u,draft_.controllerDeadzone+5);
        else if(Starts(action,"s.device.")){
            const auto player=action.size()>9&&action[9]=='1'?1u:0u;auto& index=player?draft_.controller2:draft_.controller1;
            index=std::max(-1,std::min(7,index+(Ends(action,".up")?1:-1)));
        }else if(action=="s.bindings"){SetPage(Page::Bindings);}
        else if(action=="s.root.add"){OpenBrowser(BrowserMode::Root);}
        else if(action=="s.root.previous"){if(listOffset_>=5)listOffset_-=5;ui_.ClearFocus();}
        else if(action=="s.root.next"){if(listOffset_+5<draft_.libraryRoots.size())listOffset_+=5;ui_.ClearFocus();}
        else if(Starts(action,"s.root.")){
            const auto split=action.find('.',7);if(split==std::string::npos)return true;
            const auto index=Integer(action.substr(7,split-7),0,255);if(index>=draft_.libraryRoots.size())return true;
            const auto operation=action.substr(split+1);
            if(operation=="enabled")draft_.libraryRoots[index].enabled=!draft_.libraryRoots[index].enabled;
            else if(operation=="recursive")draft_.libraryRoots[index].recursive=!draft_.libraryRoots[index].recursive;
            else if(operation=="remove"){draft_.libraryRoots.erase(draft_.libraryRoots.begin()+index);ui_.ClearFocus();}
            else if(operation=="path")EditText("ROM FOLDER PATH",draft_.libraryRoots[index].path,[this,index](std::string value){
                auto next=draft_;if(index>=next.libraryRoots.size())return;next.libraryRoots[index].path=value;Settings::Validate(next);draft_=std::move(next);
            },Page::Settings);
        }else if(action=="s.online")draft_.onlineMetadata=!draft_.onlineMetadata;
        else if(action=="s.metadata.refresh"){if(metadata_)metadata_->RefreshCatalog();notice_="CATALOG REFRESH REQUESTED";}
        else if(action=="s.metadata.clear"){if(metadata_)metadata_->ClearCache();covers_.clear();coverViewport_.clear();notice_="ARTWORK CACHE CLEAR REQUESTED";}
        else if(action=="s.open.config")Platform::OpenExternal(Storage::Parent(options_.configPath));
        else if(action=="s.open.data")Platform::OpenExternal(dataDir_);
        else if(action=="s.open.cache")Platform::OpenExternal(cacheDir_);
        else if(action=="s.reload.config")return DispatchSettings("error.reload");
        else if(action=="s.about.details")SetPage(Page::About);
        else if(action=="s.save")SaveSettings();
        else if(action=="s.cancel"){if(DraftDirty())SetPage(Page::Unsaved);else SetPage(settingsBack_);}
        else if(action=="s.unsaved.stay"){if(pendingStop_=="settingsquit")pendingStop_.clear();SetPage(Page::Settings);}
        else if(action=="s.unsaved.discard"){draft_=settings_;if(pendingStop_=="settingsquit"){pendingStop_.clear();RequestQuit();}else SetPage(settingsBack_);}
        else if(action=="s.unsaved.save"){SaveSettings();if(page_!=Page::Error){if(pendingStop_=="settingsquit"){pendingStop_.clear();RequestQuit();}else SetPage(settingsBack_);}}
        return true;
    }
    if(Starts(action,"bind.")){
        if(Starts(action,"bind.player.")){bindingPlayer_=unsigned(SuffixIndex(action,"bind.player."))%2;ui_.ClearFocus();}
        else if(action=="bind.keyboard"){bindingDevice_=Input::Device::Keyboard;ui_.ClearFocus();}
        else if(action=="bind.controller"){bindingDevice_=Input::Device::Controller;ui_.ClearFocus();}
        else if(action=="bind.done")SetPage(Page::Settings);
        else if(Starts(action,"bind.clear.")){
            const auto target=SuffixIndex(action,"bind.clear.");
            if(target<Input::ActionCount)(bindingDevice_==Input::Device::Keyboard?draft_.bindings[bindingPlayer_].keyboard:draft_.bindings[bindingPlayer_].controller)[target]="none";
            notice_="BINDING CLEARED IN DRAFT";
        }
        else if(action=="bind.reset"){const auto defaults=Input::DefaultProfiles();(bindingDevice_==Input::Device::Keyboard?draft_.bindings[bindingPlayer_].keyboard:draft_.bindings[bindingPlayer_].controller)=
            bindingDevice_==Input::Device::Keyboard?defaults[bindingPlayer_].keyboard:defaults[bindingPlayer_].controller;notice_="BINDINGS RESET IN DRAFT";}
        else if(Starts(action,"bind.action.")){
            bindingAction_=unsigned(SuffixIndex(action,"bind.action."));if(bindingAction_>=Input::ActionCount)return true;
            const auto index=bindingPlayer_?draft_.controller2:draft_.controller1;
            if(bindingDevice_==Input::Device::Controller&&(index<0||!frame_.devices[unsigned(index)].connected)){Error("Controller not connected","Choose a connected controller for this player on the Input settings tab, then return to bindings.",Page::Bindings);return true;}
            capture_.Begin(bindingDevice_,index);captureStatus_="Release controls, then press the new input.";SetPage(Page::Capture);
        }
        return true;
    }
    if(Starts(action,"capture.")){
        if(action=="capture.cancel"){capture_.Cancel();SetPage(Page::Bindings);ui_.Focus("bind.action."+std::to_string(bindingAction_));}
        else if(action=="capture.swap"){std::string error;if(!Input::Assign(draft_.bindings[bindingPlayer_],bindingDevice_,bindingAction_,captured_,true,&error))throw std::runtime_error(error);
            SetPage(Page::Bindings);ui_.Focus("bind.action."+std::to_string(bindingAction_));}
        return true;
    }
    if(Starts(action,"text.")){
        if(action=="text.cancel"){textFinish_={};SetPage(textBack_);}
        else if(action=="text.apply"){auto finish=std::move(textFinish_);const auto back=textBack_;const auto value=textValue_;SetPage(back);
            try{if(finish)finish(value);}catch(const std::exception& error){Error("Value was not applied",error.what(),back);}}
        else if(action=="text.case")textUpper_=!textUpper_;
        else if(action=="text.symbols")textSymbols_=!textSymbols_;
        else if(action=="text.clear")textValue_.clear();
        else if(action=="text.backspace"){auto text=Ui::Canvas::Decode(textValue_);if(!text.empty())text.pop_back();textValue_=Ui::Canvas::Encode(text);}
        else if(action=="text.space"){auto text=Ui::Canvas::Decode(textValue_);if(text.size()<256){text.push_back(' ');textValue_=Ui::Canvas::Encode(text);}}
        else if(Starts(action,"text.key.")){auto text=Ui::Canvas::Decode(textValue_);if(text.size()<256){text.push_back(Integer(action.substr(9),32,126));textValue_=Ui::Canvas::Encode(text);}}
        return true;
    }
    if(Starts(action,"filter.")){
        if(action=="filter.region")query_.region=NextValue(FilterValues(snapshot_,false),query_.region);
        else if(action=="filter.genre")query_.genre=NextValue(FilterValues(snapshot_,true),query_.genre);
        else if(action=="filter.region.text"||action=="filter.genre.text"){const bool region=action=="filter.region.text";
            EditText(region?"FILTER REGION (EMPTY = ALL)":"FILTER GENRE (EMPTY = ALL)",region?query_.region:query_.genre,[this,region](std::string value){(region?query_.region:query_.genre)=value;RefreshQuery();},Page::Filters);return true;
        }else if(action=="filter.year.text"){EditText("FILTER YEAR (0 = ALL)",std::to_string(query_.year),[this](std::string value){const auto year=value.empty()?0:Integer(value,0,2100);if(year&&year<1900)throw std::invalid_argument("Enter 1900..2100, or 0 for all years");query_.year=int(year);RefreshQuery();},Page::Filters);return true;}
        else if(action=="filter.year"){if(snapshot_){const auto year=std::find(snapshot_->years.begin(),snapshot_->years.end(),query_.year);query_.year=year==snapshot_->years.end()?(snapshot_->years.empty()?0:snapshot_->years.front()):(year+1==snapshot_->years.end()?0:*(year+1));}else query_.year=0;}
        else if(action=="filter.favorites")query_.favoritesOnly=!query_.favoritesOnly;
        else if(action=="filter.recent")query_.recentOnly=!query_.recentOnly;
        else if(action=="filter.available")query_.includeUnavailable=!query_.includeUnavailable;
        else if(action=="filter.sort")query_.sort=query_.sort==Library::Sort::Title?Library::Sort::LastPlayed:query_.sort==Library::Sort::LastPlayed?Library::Sort::MostPlayed:Library::Sort::Title;
        else if(action=="filter.clear"){const auto search=query_.search;query_={};query_.search=search;}
        else if(action=="filter.done"){SetPage(Page::Library);return true;}
        RefreshQuery();return true;
    }
    if(Starts(action,"match.")){
        if(action=="match.back"){listOffset_=(variant_/9)*9;SetPage(Page::Details);}
        else if(action=="match.search"){EditText("SEARCH CATALOG",selected_.metadata.title.empty()?selected_.title:selected_.metadata.title,[this](std::string value){const auto catalog=metadata_?metadata_->Snapshot():nullptr;matches_=catalog?catalog->Search(value,100):std::vector<Metadata::Game>{};listOffset_=0;},Page::Match);}
        else if(action=="match.refresh"){if(metadata_)metadata_->RefreshCatalog();notice_="CATALOG REFRESH REQUESTED";}
        else if(action=="match.previous"){if(listOffset_>=6)listOffset_-=6;ui_.ClearFocus();}
        else if(action=="match.next"){if(listOffset_+6<matches_.size())listOffset_+=6;ui_.ClearFocus();}
        else if(action=="match.auto"){Library::Metadata automatic;automatic.artworkPath=selected_.metadata.artworkPath;if(index_)index_->ResetManualMetadata(selected_.id);selected_.metadata=automatic;selected_.manualOverride=false;matched_.erase(selected_.id);coverViewport_.clear();listOffset_=(variant_/9)*9;SetPage(Page::Details);RefreshQuery(false);}
        else if(Starts(action,"match.choose.")){const auto index=SuffixIndex(action,"match.choose.");if(index<matches_.size()){ApplyMetadata(matches_[index],true);selected_.manualOverride=true;listOffset_=(variant_/9)*9;SetPage(Page::Details);RefreshQuery(false);}}
        return true;
    }
    if(Starts(action,"error.")){
        if(action=="error.exit")RequestQuit();
        else if(action=="error.openconfig")Platform::OpenExternal(Storage::Parent(options_.configPath));
        else if(action=="error.install")Platform::OpenExternal("https://www.7-zip.org/");
        else if(action=="error.open")OpenBrowser(BrowserMode::Game);
        else if(action=="error.settings"){draft_=settings_;settingsBack_=errorBack_;SetPage(Page::Settings);}
        else if(action=="error.back")SetPage(errorBack_);
        else if(action=="error.retry"){const auto retry=retry_;SetPage(errorBack_);if(retry)retry();if(page_==Page::Unsaved&&!DraftDirty()){if(pendingStop_=="settingsquit"){pendingStop_.clear();RequestQuit();}else SetPage(settingsBack_);}}
        else if(action=="error.reload"){
            const auto old=settings_;
            try{LoadSettings();try{RecreateWindow();}catch(...){settings_=old;RecreateWindow();throw;}
                ConfigureLibrary(!session_.Active());if(metadata_){Metadata::Options online;online.enabled=settings_.onlineMetadata&&!session_.Active();metadata_->Configure(online);metadataEnabled_=online.enabled;}
                auto host=settings_;if(options_.noAudio)host.audioEnabled=false;AudioOutput::ApplySettings(host);AudioOutput::Suspend();
                notice_="SETTINGS RELOADED";SetPage(session_.Active()?Page::Paused:Page::Library);
            }catch(const std::exception& error){settingsError_=error.what();configDefaultsOnce_=false;Error("Settings need attention",settingsError_,Page::Library);}
        }else if(action=="error.defaults"){
            document_.reset();settings_=Settings::Values();draft_=settings_;configDefaultsOnce_=true;RecreateWindow();ConfigureLibrary(!session_.Active());
            if(metadata_){Metadata::Options online;online.enabled=settings_.onlineMetadata&&!session_.Active();metadata_->Configure(online);metadataEnabled_=online.enabled;}
            notice_="TEMPORARY DEFAULTS - ORIGINAL SETTINGS FILE UNCHANGED";SetPage(Page::Library);
        }
        return true;
    }
    return false;
}
}}
