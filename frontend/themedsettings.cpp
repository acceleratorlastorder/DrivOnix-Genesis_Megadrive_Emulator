#include "application_internal.hpp"

namespace Frontend { namespace Detail {
namespace {
std::string State(bool value){return value?"On":"Off";}
std::string Capitals(std::string value){for(char& c:value)if(c>='a'&&c<='z')c=char(c-'a'+'A');return value;}
std::string Device(int index,const Input::Frame& frame){
    if(index<0)return "Disabled";
    const auto& device=frame.devices[unsigned(index)];
    return std::to_string(index)+(device.connected?" · "+device.name:" · Not connected");
}
std::string Active(const std::set<std::string>& values){
    std::string text;for(const auto& value:values){if(value.empty()||Starts(value,"axis_")||Starts(value,"button_"))continue;
        if(!text.empty())text+="  ";
        text+=value;}
    return text.empty()?"Neutral":text;
}
// This content height is deliberately larger than the viewport in small
// windows. Canvas scrolls focused controls into view for keyboard and gamepad.
struct Form {
    Ui::Canvas& ui;float x,y,width;const Ui::Palette& colors;
    Form(Ui::Canvas& canvas,const std::string& id,sfFloatRect area,float height):
        ui(canvas),x(area.left+12),y(area.top+12),width(area.width-24),colors(canvas.Colors()){
        ui.Rect(area,colors.panel,colors.border);ui.BeginScroll(id,area,height+24);
    }
    ~Form(){ui.EndScroll();}
    void Heading(const std::string& title){ui.Text(title,x,y,24,colors.text,width);y+=42;}
    void Note(const std::string& text,float height=48){ui.Wrap(text,{x,y,width,height},14,colors.muted);y+=height+12;}
    void Button(const std::string& id,const std::string& label,bool enabled=true,Ui::Tone tone=Ui::Tone::Normal){
        ui.Button(id,label,{x,y,width,40},enabled,tone,16);y+=50;
    }
    void Pair(const std::string& id1,const std::string& text1,const std::string& id2,const std::string& text2,
              bool enabled1=true,bool enabled2=true,Ui::Tone tone1=Ui::Tone::Normal,Ui::Tone tone2=Ui::Tone::Normal){
        const float half=(width-12)/2;
        ui.Button(id1,text1,{x,y,half,40},enabled1,tone1,14);
        ui.Button(id2,text2,{x+half+12,y,half,40},enabled2,tone2,14);y+=50;
    }
};
}

void Application::DrawAppearance(){
    Header("THEME & APPEARANCE");
    const auto& c=ui_.Context();const auto& p=ui_.Colors();
    const Ui::ThemeId themes[]={Ui::ThemeId::Retronix,Ui::ThemeId::RetronixHd,Ui::ThemeId::Modern};
    const unsigned scales[]={100,125,150,200};
    if(c.theme==Ui::ThemeId::Retronix){
        for(unsigned i=0;i<3;++i){const auto& theme=Ui::Theme(themes[i]);const float y=57+float(i)*52;
            ui_.Button(std::string("appearance.theme.")+theme.key,Capitals(theme.name),{24,y,592,29},true,
                draft_.theme==theme.key?Ui::Tone::Accent:Ui::Tone::Normal,15);
            ui_.Text(theme.description,28,y+33,11,p.muted,584);
        }
        ui_.Text("NATIVE INTERFACE SIZE",25,218,13,p.text);
        for(unsigned i=0;i<4;++i)ui_.Button("appearance.scale."+std::to_string(scales[i]),std::to_string(scales[i])+"%",
            {24+float(i)*151,242,139,29},false,scales[i]==draft_.uiScale?Ui::Tone::Accent:Ui::Tone::Quiet,14);
        ui_.Text("Choose HD or Modern to change interface size.",26,278,11,p.muted,588);
        if(!appearanceError_.empty())ui_.Text(appearanceError_,26,292,11,p.red,588);
        ui_.Button("appearance.back","BACK TO SETTINGS",{180,308,280,25},true,Ui::Tone::Accent,12);
    }else{
        {
            Form form(ui_,"appearance.body",{24,80,c.width-48,c.height-168},580);
            form.Note("Preview an appearance, then Save & Apply in Settings to keep it. Discard returns to your saved appearance.",44);
            for(const auto id:themes){const auto& theme=Ui::Theme(id);
                form.Button(std::string("appearance.theme.")+theme.key,theme.name,true,draft_.theme==theme.key?Ui::Tone::Accent:Ui::Tone::Normal);
                form.Note(theme.description,24);
            }
            form.Heading("Interface size");
            const float unit=(form.width-36)/4;
            for(unsigned i=0;i<4;++i)ui_.Button("appearance.scale."+std::to_string(scales[i]),std::to_string(scales[i])+"%",
                {form.x+float(i)*(unit+12),form.y,unit,40},true,scales[i]==draft_.uiScale?Ui::Tone::Accent:Ui::Tone::Normal,16);
            form.y+=52;
            form.Note("Small windows limit the effective scale to keep controls accessible. Your chosen size returns when there is room.",44);
            if(!appearanceError_.empty())ui_.Wrap(appearanceError_,{form.x,form.y,form.width,80},14,p.red);
        }
        ui_.Button("appearance.back","Back to settings",{24,c.height-70,c.width-48,40},true,Ui::Tone::Accent,16);
    }
    Footer("Appearance previews stay in your settings draft until Save & Apply.");
}

void Application::DrawThemedSettings(){
    const auto& c=ui_.Context();const auto& p=ui_.Colors();
    Header(page_==Page::Unsaved?"UNSAVED SETTINGS":"SETTINGS");
    if(page_==Page::Unsaved){
        {
            Form form(ui_,"s.unsaved.body",{24,80,c.width-48,c.height-112},300);
            form.Heading("Keep your changes?");
            form.Note("Your settings and appearance changes have not been saved. Choose what to do before leaving.",50);
            form.Button("s.unsaved.save","Save & return",document_!=nullptr,Ui::Tone::Accent);
            form.Button("s.unsaved.discard","Discard changes",true,Ui::Tone::Danger);
            form.Button("s.unsaved.stay","Keep editing");
        }
        Footer("The existing INI stays unchanged until a save succeeds.");return;
    }
    const char* tabs[]={"Display","Audio","Input","Folders","Online","About"};
    const bool sidebar=c.theme==Ui::ThemeId::Modern;
    float left=24,top=132,width=c.width-48;
    if(sidebar){
        const float sidebarWidth=c.compact?154.f:200.f;
        ui_.BeginScroll("s.navigation",{16,80,sidebarWidth,c.height-168},288);
        for(unsigned tab=0;tab<6;++tab)ui_.Button("s.tab."+std::to_string(tab),tabs[tab],
            {16,80+float(tab)*48,sidebarWidth,40},true,settingsTab_==tab?Ui::Tone::Accent:Ui::Tone::Quiet,16);
        ui_.EndScroll();left=sidebarWidth+32;top=80;width=c.width-left-24;
    }else{
        const float tabWidth=(c.width-48-5*8)/6;
        for(unsigned tab=0;tab<6;++tab)ui_.Button("s.tab."+std::to_string(tab),tabs[tab],
            {24+float(tab)*(tabWidth+8),80,tabWidth,38},true,settingsTab_==tab?Ui::Tone::Accent:Ui::Tone::Quiet,14);
    }
    const float contentHeight=settingsTab_==0?450.f:settingsTab_==2?470.f:settingsTab_==3?
        170.f+float(Capacity(Ui::ListKind::Folders))*62.f:settingsTab_==4||settingsTab_==5?470.f:350.f;
    {
        Form form(ui_,"s.body."+std::to_string(settingsTab_),{left,top,width,c.height-top-88},contentHeight);
        form.Heading(tabs[settingsTab_]);
        switch(settingsTab_){
        case 0:
            form.Pair("s.fullscreen","Fullscreen: "+State(draft_.fullscreen),"s.fit",draft_.integerFit?"Game: integer fit":"Game: fixed scale");
            form.Note(draft_.integerFit?"The game uses the largest whole-pixel image that fits.":"Your fixed game scale is fitted inside the window.",40);
            form.Pair("s.scale.down","− Fixed scale: "+std::to_string(draft_.scale)+"×","s.scale.up","Increase scale +",draft_.scale>1,draft_.scale<8);
            form.Button("s.bios","BIOS boot: "+State(draft_.showBios));
            form.Pair("s.width","Window width: "+std::to_string(draft_.windowWidth),"s.height","Height: "+std::to_string(draft_.windowHeight));
            form.Button("s.size.preset","Next window size preset");
            form.Button("s.appearance","Theme & Appearance",true,Ui::Tone::Accent);break;
        case 1:
            form.Button("s.audio","Host audio: "+State(draft_.audioEnabled));
            form.Note("Volume: "+std::to_string(draft_.volume)+"%",24);
            ui_.Rect({form.x,form.y,form.width,10},p.input,p.border);
            ui_.Rect({form.x+1,form.y+1,(form.width-2)*float(draft_.volume)/100.f,8},p.green);form.y+=28;
            form.Pair("s.volume.down","− 5%","s.volume.up","+ 5%",draft_.volume>0,draft_.volume<100);
            form.Note(options_.noAudio?"This launch uses --no-audio. Playback stays muted while your file preference is saved.":"Sound-chip execution continues when host playback is disabled.",60);break;
        case 2:
            form.Button("s.controllers","Game controllers: "+State(draft_.controllersEnabled));
            form.Note("Stick deadzone: "+std::to_string(draft_.controllerDeadzone)+"%",24);
            form.Pair("s.deadzone.down","− Deadzone","s.deadzone.up","Deadzone +",draft_.controllerDeadzone>1,draft_.controllerDeadzone<95);
            for(unsigned player=0;player<2;++player){const auto index=player?draft_.controller2:draft_.controller1;
                ui_.Text("Player "+std::to_string(player+1)+": "+Device(index,frame_),form.x,form.y,14,p.text,form.width);form.y+=28;
                form.Pair("s.device."+std::to_string(player)+".down","< Previous device","s.device."+std::to_string(player)+".up","Next device >",index>-1,index<7);
            }
            form.Button("s.bindings","Keyboard & controller bindings",true,Ui::Tone::Accent);
            form.Note("Menu navigation stays available. R3 / Escape / F1 opens the menu; F11 toggles fullscreen.",50);break;
        case 3:{
            const auto count=Capacity(Ui::ListKind::Folders);
            if(listOffset_>=draft_.libraryRoots.size())listOffset_=draft_.libraryRoots.empty()?0:((draft_.libraryRoots.size()-1)/count)*count;
            form.Button("s.root.add","Add folder · "+std::to_string(draft_.libraryRoots.size())+" configured",draft_.libraryRoots.size()<256,Ui::Tone::Accent);
            for(std::size_t i=listOffset_;i<draft_.libraryRoots.size()&&i<listOffset_+count;++i){
                const auto& root=draft_.libraryRoots[i];const auto prefix="s.root."+std::to_string(i)+".";
                ui_.Text(root.path,form.x,form.y,14,p.text,form.width);
                const float actionWidth=88,pathWidth=form.width-3*(actionWidth+8);
                ui_.Button(prefix+"path","Edit path",{form.x,form.y+20,pathWidth,38},true,Ui::Tone::Quiet,14);
                ui_.Button(prefix+"enabled",root.enabled?"Enabled":"Disabled",{form.x+pathWidth+8,form.y+20,actionWidth,38},true,root.enabled?Ui::Tone::Accent:Ui::Tone::Quiet,14);
                ui_.Button(prefix+"recursive",root.recursive?"Subdirs on":"Subdirs off",{form.x+pathWidth+actionWidth+16,form.y+20,actionWidth,38},true,Ui::Tone::Quiet,14);
                ui_.Button(prefix+"remove","Remove",{form.x+pathWidth+2*actionWidth+24,form.y+20,actionWidth,38},true,Ui::Tone::Danger,14);form.y+=62;
            }
            if(draft_.libraryRoots.empty())form.Note("Add a folder containing ROMs or archives. Your source files stay in place.",48);
            form.y+=8;form.Pair("s.root.previous","< Previous","s.root.next","Next >",listOffset_>0,listOffset_+count<draft_.libraryRoots.size());
            break;
        }
        case 4:
            form.Button("s.online","Online information & artwork: "+State(draft_.onlineMetadata));
            form.Note("Local games work offline. Downloads supply information and artwork; ROM files stay local.",58);
            form.Note(metadata_&&metadata_->Snapshot()?"Catalog available":"Catalog not available yet",24);
            form.Button("s.metadata.refresh","Refresh catalog",settings_.onlineMetadata&&metadata_!=nullptr);
            form.Button("s.metadata.clear","Clear artwork cache",metadata_!=nullptr);
            form.Button("s.open.cache","Open cache folder");break;
        default:
            form.Note("DrivOnix "+version_,28);form.Note("Settings file",20);
            ui_.Wrap(options_.configPath,{form.x,form.y,form.width,64},16,p.text);form.y+=76;
            form.Note(configDefaultsOnce_?"Temporary defaults. Your original file is unchanged.":"The file and interface share the same typed settings.",44);
            form.Pair("s.open.config","Settings folder","s.open.data","Data folder");
            if(configDefaultsOnce_)form.Button("s.reload.config","Reload repaired settings",true,Ui::Tone::Accent);
            else form.Button("s.about.details","Credits & licenses");
            break;
        }
    }
    bool dirty=true;try{dirty=DraftDirty();}catch(...){}
    const float actionWidth=std::min(280.f,(c.width-144)/2);
    ui_.Button("s.save","Save & Apply",{24,c.height-70,actionWidth,40},document_!=nullptr,Ui::Tone::Accent,16);
    ui_.Button("s.cancel",dirty?"Cancel / Back":"Back",{c.width-actionWidth-24,c.height-70,actionWidth,40},true,Ui::Tone::Normal,16);
    ui_.Text(dirty?"Unsaved":"Saved",c.width/2-36,c.height-57,14,dirty?p.green:p.muted,72);
    Footer(configDefaultsOnce_?"Repair and reload the original settings file to save changes.":"Changes remain a draft until Save & Apply.  B / Esc: Back");
}

void Application::DrawThemedBindings(){
    const auto& c=ui_.Context();const auto& p=ui_.Colors();
    Header("PLAYER "+std::to_string(bindingPlayer_+1)+" GAME BINDINGS");
    if(page_==Page::Capture||page_==Page::Conflict){
        {
            Form form(ui_,"capture.body",{24,80,c.width-48,c.height-168},240);
            if(page_==Page::Capture){
                form.Heading("Assign "+Capitals(Input::ActionName(bindingAction_)));
                form.Note(captureStatus_.empty()?"Release controls, then press a key, button or move an axis.":captureStatus_,72);
                form.Note("Escape / F1 / R3 cancels. These reserved controls cannot drive the game.",48);
            }else{
                const auto conflicts=Input::Conflicts(draft_.bindings[bindingPlayer_],bindingDevice_,bindingAction_,captured_);
                std::string names;for(auto action:conflicts){if(!names.empty())names+=", ";names+=Capitals(Input::ActionName(action));}
                form.Heading("This control is already used");
                form.Note(captured_+" is assigned to "+names+". "+(conflicts.size()==1?"Swap the actions, or keep the original bindings.":"Clear the conflicting bindings first, or choose another control."),110);
            }
        }
        if(page_==Page::Conflict){
            const auto conflicts=Input::Conflicts(draft_.bindings[bindingPlayer_],bindingDevice_,bindingAction_,captured_);
            ui_.Button("capture.swap","Swap actions",{24,c.height-70,(c.width-60)/2,40},conflicts.size()==1,Ui::Tone::Accent,16);
            ui_.Button("capture.cancel","Cancel",{(c.width+12)/2,c.height-70,(c.width-60)/2,40},true,Ui::Tone::Normal,16);
        }else ui_.Button("capture.cancel","Cancel",{24,c.height-70,c.width-48,40},true,Ui::Tone::Normal,16);
        Footer("Only fresh input is captured. The game remains paused.");return;
    }
    const float tabWidth=(c.width-72)/4;
    const char* tabIds[]={"bind.player.0","bind.player.1","bind.keyboard","bind.controller"};
    const char* tabNames[]={"Player 1","Player 2","Keyboard","Controller"};
    for(unsigned i=0;i<4;++i){const bool selected=i<2?bindingPlayer_==i:(i==2)==(bindingDevice_==Input::Device::Keyboard);
        ui_.Button(tabIds[i],tabNames[i],{24+float(i)*(tabWidth+8),80,tabWidth,38},true,selected?Ui::Tone::Accent:Ui::Tone::Quiet,14);}
    const unsigned actions[]={Input::Up,Input::Down,Input::Left,Input::Right,Input::Start,Input::Mode,Input::A,Input::B,Input::C,Input::X,Input::Y,Input::Z};
    const bool columns=c.width>=900;const float contentHeight=float(columns?6:12)*48;
    {
        Form form(ui_,"bind.actions",{24,132,c.width-48,c.height-276},contentHeight);
        const auto& profile=draft_.bindings[bindingPlayer_];const auto& list=bindingDevice_==Input::Device::Keyboard?profile.keyboard:profile.controller;
        const float width=columns?(form.width-16)/2:form.width;const float top=form.y;
        for(unsigned i=0;i<12;++i){const auto action=actions[i];const float x=form.x+(columns?float(i/6)*(width+16):0);
            const float y=top+float(columns?i%6:i)*48;
            ui_.Button("bind.action."+std::to_string(action),Capitals(Input::ActionName(action))+" · "+list[action],{x,y,width-74,40},true,Ui::Tone::Normal,16);
            ui_.Button("bind.clear."+std::to_string(action),"Clear",{x+width-66,y,66,40},true,Ui::Tone::Quiet,14);
        }
    }
    if(bindingDevice_==Input::Device::Keyboard)ui_.Wrap("Live keys: "+Active(frame_.keys),{24,c.height-132,c.width-48,54},14,p.green);
    else{
        const auto index=bindingPlayer_?draft_.controller2:draft_.controller1;
        ui_.Text("Live: "+Device(index,frame_),24,c.height-132,14,index>=0&&frame_.devices[unsigned(index)].connected?p.green:p.red,c.width-48);
        if(index>=0){const auto& state=frame_.devices[unsigned(index)];
            ui_.Text("L "+std::to_string(state.leftX)+" / "+std::to_string(state.leftY)+"   R "+std::to_string(state.rightX)+" / "+std::to_string(state.rightY)+
                "   LT / RT "+std::to_string(state.leftTrigger)+" / "+std::to_string(state.rightTrigger),24,c.height-110,14,p.muted,c.width-48);
            ui_.Text(Active(Input::ControllerSymbols(state,draft_.controllerDeadzone)),24,c.height-88,14,p.green,c.width-48);
        }
    }
    ui_.Button("bind.reset","Restore this tab",{24,c.height-70,(c.width-60)/2,40},true,Ui::Tone::Normal,16);
    ui_.Button("bind.done","Done",{(c.width+12)/2,c.height-70,(c.width-60)/2,40},true,Ui::Tone::Accent,16);
    Footer("Choose an action to rebind it. Keyboard and controller bindings stay independent.");
}

void Application::DrawThemedTextEditor(){
    const auto& c=ui_.Context();const auto& p=ui_.Colors();Header(textTitle_);
    ui_.Rect({24,80,c.width-48,64},p.input,p.green);
    auto visible=Ui::Canvas::Decode(textValue_);const auto count=std::size_t((c.width-80)/8)*2;
    if(visible.size()>count){visible.erase(visible.begin(),visible.end()-count);visible.insert(visible.begin(),0x2026);}
    ui_.Wrap(Ui::Canvas::Encode(visible)+"|",{36,90,c.width-72,48},16,p.text);
    const char* letters[]={"qwertyuiop","asdfghjkl'","zxcvbnm/\\?"};
    const char* symbols[]={"1234567890","-_./\\:;#\"'","!@$%&()[]+"};
    ui_.BeginScroll("text.keyboard",{24,160,c.width-48,c.height-248},242);
    const float keyWidth=(c.width-48-9*8)/10;
    for(unsigned row=0;row<3;++row){std::string keys=textSymbols_?symbols[row]:letters[row];if(textUpper_)keys=Capitals(keys);
        for(unsigned column=0;column<keys.size();++column){const auto key=static_cast<unsigned char>(keys[column]);
            ui_.Button("text.key."+std::to_string(key),std::string(1,char(key)),{24+float(column)*(keyWidth+8),160+float(row)*48,keyWidth,40},true,Ui::Tone::Normal,16);}}
    const float unit=(c.width-72)/4;
    ui_.Button("text.space","Space",{24,308,unit,40},true,Ui::Tone::Normal,14);
    ui_.Button("text.backspace","Backspace",{32+unit,308,unit,40},true,Ui::Tone::Normal,14);
    ui_.Button("text.case",textUpper_?"Lowercase":"Uppercase",{40+2*unit,308,unit,40},true,Ui::Tone::Normal,14);
    ui_.Button("text.symbols",textSymbols_?"ABC":"123 / #",{48+3*unit,308,unit,40},true,Ui::Tone::Normal,14);
    ui_.Button("text.clear","Clear text",{24,358,unit,40},!textValue_.empty(),Ui::Tone::Quiet,14);
    ui_.Text("Physical keyboard and Unicode text entry are supported.",unit+40,370,14,p.muted,c.width-unit-64);
    ui_.EndScroll();
    ui_.Button("text.apply","Apply",{24,c.height-70,(c.width-60)/2,40},true,Ui::Tone::Accent,16);
    ui_.Button("text.cancel","Cancel",{(c.width+12)/2,c.height-70,(c.width-60)/2,40},true,Ui::Tone::Normal,16);
    if(frame_.keys.count("enter"))ui_.Focus("text.apply");
    Footer("D-pad: Choose key   A: Type / Activate   B: Cancel   Enter: Apply");
}

void Application::DrawThemedFilters(){
    const auto& c=ui_.Context();Header("LIBRARY FILTERS");
    {
        Form form(ui_,"filter.body",{24,80,c.width-48,c.height-168},380);
        form.Pair("filter.region","Region: "+(query_.region.empty()?std::string("All"):query_.region),"filter.region.text","Type region…");
        form.Pair("filter.genre","Genre: "+(query_.genre.empty()?std::string("All"):query_.genre),"filter.genre.text","Type genre…");
        form.Pair("filter.year","Year: "+(query_.year?std::to_string(query_.year):std::string("All")),"filter.year.text","Type year…");
        const auto sort=query_.sort==Library::Sort::Title?"Title":query_.sort==Library::Sort::LastPlayed?"Last played":"Most played";
        form.Button("filter.sort",std::string("Sort: ")+sort);
        form.Pair("filter.favorites","Favorites only: "+State(query_.favoritesOnly),"filter.recent","Recent only: "+State(query_.recentOnly));
        form.Button("filter.available","Show unavailable: "+State(query_.includeUnavailable));
        form.Note("Choose a value to cycle through all indexed games, or type a specific value.",44);
    }
    ui_.Button("filter.clear","Reset filters",{24,c.height-70,(c.width-60)/2,40},true,Ui::Tone::Normal,16);
    ui_.Button("filter.done","Done",{(c.width+12)/2,c.height-70,(c.width-60)/2,40},true,Ui::Tone::Accent,16);
    Footer("Filters update the local library. Search text is kept when filters are reset.");
}

void Application::DrawThemedMatch(){
    const auto& c=ui_.Context();const auto& p=ui_.Colors();Header("MATCH GAME INFORMATION");
    ui_.Button("match.search","Search catalog…",{24,80,c.width-344,38},metadata_&&metadata_->Snapshot()!=nullptr,Ui::Tone::Accent,16);
    ui_.Button("match.refresh","Refresh",{c.width-308,80,132,38},metadata_&&settings_.onlineMetadata,Ui::Tone::Normal,16);
    ui_.Button("match.back","Back",{c.width-164,80,140,38},true,Ui::Tone::Normal,16);
    ui_.Text(selected_.metadata.title.empty()?selected_.title:selected_.metadata.title,24,130,16,p.green,c.width-48);
    const auto count=Capacity(Ui::ListKind::Matches);
    if(listOffset_>=matches_.size())listOffset_=matches_.empty()?0:((matches_.size()-1)/count)*count;
    ui_.BeginScroll("match.results",{24,158,c.width-48,c.height-246},float(count)*40);
    if(!metadata_||!metadata_->Snapshot())ui_.Wrap(settings_.onlineMetadata?"The catalog is still loading. Refresh it or return to the library.":"No cached catalog is available. Enable online information in Settings to fetch it.",{36,170,c.width-72,100},16,p.muted);
    else if(matches_.empty())ui_.Wrap("No matching entries. Search another title, or keep the local information.",{36,170,c.width-72,100},16,p.muted);
    for(std::size_t i=listOffset_;i<matches_.size()&&i<listOffset_+count;++i){const auto& match=matches_[i];
        const auto detail=(match.region.empty()?"":" ["+match.region+"]")+(match.year?" · "+std::to_string(match.year):"");
        ui_.Button("match.choose."+std::to_string(i),match.title+detail,{24,158+float(i-listOffset_)*40,c.width-48,36},true,Ui::Tone::Normal,16);}
    ui_.EndScroll();
    ui_.Button("match.previous","< Previous",{24,c.height-70,124,40},listOffset_>0,Ui::Tone::Normal,14);
    ui_.Button("match.next","Next >",{160,c.height-70,108,40},listOffset_+count<matches_.size(),Ui::Tone::Normal,14);
    ui_.Button("match.auto","Use automatic matching",{280,c.height-70,c.width-304,40},true,Ui::Tone::Normal,14);
    Footer("A manual information override keeps ROMs and battery saves separate.");
}

void Application::DrawThemedError(){
    const auto& c=ui_.Context();const auto& p=ui_.Colors();Header(errorTitle_.empty()?"SOMETHING NEEDS ATTENTION":errorTitle_);
    const float messageHeight=std::max(170.f,(float(Ui::Canvas::Decode(errorMessage_).size())/std::max(1.f,(c.width-72)/9.f)+float(std::count(errorMessage_.begin(),errorMessage_.end(),'\n')))*24+64);
    ui_.Rect({24,80,c.width-48,c.height-218},p.panel,p.red);
    ui_.BeginScroll("error.message",{24,80,c.width-48,c.height-218},messageHeight);
    ui_.Wrap(errorMessage_,{36,92,c.width-72,messageHeight-24},16,p.text);ui_.EndScroll();
    const float half=(c.width-60)/2;
    if(!settingsError_.empty()&&!configDefaultsOnce_){
        ui_.Button("error.reload","Retry loading settings",{24,c.height-122,half,40},true,Ui::Tone::Accent,16);
        ui_.Button("error.openconfig","Open settings folder",{36+half,c.height-122,half,40},true,Ui::Tone::Normal,16);
        ui_.Button("error.defaults","Use defaults once",{24,c.height-72,half,40},true,Ui::Tone::Normal,16);
        ui_.Button("error.exit","Exit",{36+half,c.height-72,half,40},true,Ui::Tone::Danger,16);
        Footer("The original settings file has not been replaced. Repair it, then retry loading.");
    }else{
        ui_.Button("error.retry","Retry",{24,c.height-122,half,40},bool(retry_),Ui::Tone::Accent,16);
        ui_.Button("error.open","Choose another game",{36+half,c.height-122,half,40},true,Ui::Tone::Normal,16);
        const float third=(c.width-72)/3;const auto error=Lower(errorMessage_);
        const bool archiver=error.find("7-zip")!=std::string::npos||error.find("7zip")!=std::string::npos;
        ui_.Button("error.settings","Settings",{24,c.height-72,third,40},true,Ui::Tone::Normal,16);
        ui_.Button(archiver?"error.install":"error.back",archiver?"Get 7-Zip":"Return",{36+third,c.height-72,third,40},true,Ui::Tone::Normal,16);
        ui_.Button("error.exit","Exit",{48+2*third,c.height-72,third,40},true,Ui::Tone::Danger,16);
        Footer("Fix the issue, retry, or choose another game. Your source files stay unchanged.");
    }
}
}}
