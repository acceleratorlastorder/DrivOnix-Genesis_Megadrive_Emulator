#include "../../frontend/ui.hpp"
#include "../../platform/storage.hpp"
#include <cmath>
#include <algorithm>
#include <chrono>
#include <thread>
#include <iostream>
#include <stdexcept>

namespace {
void Need(bool okay,const char* reason){if(!okay)throw std::runtime_error(reason);}
template<class F> void Fails(F action){bool failed=false;try{action();}catch(const std::exception&){failed=true;}Need(failed,"Invalid theme/font operation was accepted");}
void Capture(sfRenderWindow* window,const std::string& path) {
    const auto size=sfRenderWindow_getSize(window);auto* texture=sfTexture_create(size.x,size.y);
    Need(texture!=nullptr,"Cannot allocate theme screenshot");sfTexture_updateFromRenderWindow(texture,window,0,0);
    auto* image=sfTexture_copyToImage(texture);sfTexture_destroy(texture);Need(image!=nullptr,"Cannot capture theme screenshot");
    const auto okay=sfImage_saveToFile(image,path.c_str());sfImage_destroy(image);Need(okay!=sfFalse,"Cannot save theme screenshot");
}
sfFloatRect Control(const Ui::Canvas& canvas,const std::string& id) {
    for(const auto& item:canvas.Controls())if(item.id==id)return item.area;
    throw std::runtime_error("Theme control was not registered: "+id);
}
void Resize(sfRenderWindow* window,sfVector2u size) {
    sfEvent event;while(sfRenderWindow_pollEvent(window,&event)){}
    const auto before=sfRenderWindow_getSize(window);sfRenderWindow_setSize(window,size);
    if(before.x==size.x&&before.y==size.y)return;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(std::chrono::steady_clock::now()<deadline){
        while(sfRenderWindow_pollEvent(window,&event))
            if(event.type==sfEvtResized&&event.size.width==size.x&&event.size.height==size.y)return;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("Native resize event did not reach the renderer");
}
void Encoding() {
    const std::string label=u8"Café 日本語 😀";
    Need(Ui::Canvas::Encode(Ui::Canvas::Decode(label))==label,"UI encoding lost Unicode or supplementary-plane text");
    const auto bad=Ui::Canvas::Decode(std::string("\xf0\x80\x80\x80",4));
    Need(bad.size()==4&&bad.front()==0xfffd,"UI accepted overlong UTF-8");
    Need(Ui::Canvas::Decode(std::string("\xed\xa0\x80",3)).front()==0xfffd,"UI accepted UTF-8 surrogate");
    Need(Ui::Canvas::Encode({0xd800,0x110000})==u8"��","UI encoded an invalid Unicode scalar");
    Need(Ui::Canvas::Decode("").empty(),"UI decoder appended a hidden terminator");
}
}
int main(int argc,char** argv) {
    try {
        Need(argc==3,"Expected font source and UI fixture directories");Encoding();
        const auto directory=Storage::Join(argv[2],u8"fonts-é-日本語");Storage::EnsureDirectory(directory);
        const auto mainFont=Storage::Join(directory,"main.otf"),upperFont=Storage::Join(directory,"upper.otf");
        Storage::CopyFileAtomic(Storage::Join(argv[1],"unifont.otf"),mainFont,16*1024*1024);
        Storage::CopyFileAtomic(Storage::Join(argv[1],"unifont_upper.otf"),upperFont,16*1024*1024);
        auto* window=sfRenderWindow_create({1280,720,32},"DrivOnix frontend UI test",sfClose|sfResize,nullptr);
        Need(window!=nullptr,"Cannot open UI fixture window");
        struct Window {sfRenderWindow* value;~Window(){sfRenderWindow_destroy(value);}} owner{window};
        Ui::Canvas ui;ui.LoadFonts(mainFont,upperFont);
        Fails([&]{ui.Configure(Ui::ThemeId::Modern);});
        Need(ui.Context().theme==Ui::ThemeId::Retronix,"Missing smooth fonts replaced the working Retronix theme");
        auto draw=[&](const Input::UiActions& actions,const Ui::Pointer& pointer=Ui::Pointer{}) {
            sfRenderWindow_clear(window,Ui::Background);ui.Begin(window,actions,pointer);
            ui.Rect({0,0,640,50},Ui::Panel);ui.Rect({10,48,620,1},Ui::Border);
            ui.Text("DRIVONIX",16,12,24,Ui::Green);ui.Text(u8"Café 日本語 😀",360,18,16,Ui::White,260);
            ui.Card("one","First game",{20,80,120,100},nullptr,true,true);
            ui.Card("two",u8"日本語 second game",{160,80,120,100},nullptr,false,true);
            ui.Button("disabled","Unavailable",{20,200,120,28},false);
            ui.Button("quit","Quit",{160,200,120,28},true,Ui::Tone::Danger);
            ui.Wrap("Pixel menus keep the library, settings and game controls readable. Unicode filenames remain exact.",{320,80,290,120},14,Ui::Muted);
            ui.Text(std::string(16384,'x'),16,280,14,Ui::White,600);
            const auto action=ui.Finish();sfRenderWindow_display(window);return action;
        };
        Need(draw({}).empty()&&ui.Focus()=="one","UI did not focus the first available action");
        Input::UiActions actions;actions.right=true;draw(actions);Need(ui.Focus()=="two","D-pad spatial navigation failed");
        actions={};actions.confirm=true;Need(draw(actions)=="two","Confirm activated the wrong game");
        actions={};actions.down=true;draw(actions);Need(ui.Focus()=="quit","Vertical focus navigation failed");
        Need(draw({},Ui::Pointer{30,210,true,true}).empty()&&ui.Focus()=="quit","Disabled UI item accepted a click");
        Need(draw({},Ui::Pointer{30,100,true,true})=="one"&&ui.Focus()=="one","Mouse click did not select its exact card");
        ui.Focus("not-present");draw({});Need(ui.Focus()=="one","Removed action left focus stranded");
        auto* capture=sfTexture_create(1280,720);Need(capture!=nullptr,"Cannot capture UI fixture");
        sfTexture_updateFromRenderWindow(capture,window,0,0);auto* image=sfTexture_copyToImage(capture);sfTexture_destroy(capture);
        Need(image!=nullptr,"Cannot read UI fixture pixels");
        const auto screenshot=Storage::Join(argv[2],"retro-ui.png");
        Need(sfImage_saveToFile(image,screenshot.c_str())!=sfFalse,"Cannot save UI fixture screenshot");sfImage_destroy(image);
        Resize(window,{800,800});
        draw({});const auto logical=ui.ToLogical({400,400});
        Need(std::abs(logical.x-320)<0.1&&std::abs(logical.y-180)<0.1,"Resized UI pointer transform lost letterboxing");
        Need(Ui::ParseTheme("retronix")==Ui::ThemeId::Retronix&&Ui::ParseTheme("retronix_hd")==Ui::ThemeId::RetronixHd&&Ui::ParseTheme("modern")==Ui::ThemeId::Modern,"Theme IDs do not round-trip");
        Fails([]{Ui::ParseTheme("unknown");});
        const auto regular=Storage::Join(directory,"regular.ttf"),medium=Storage::Join(directory,"medium.ttf"),coverage=Storage::Join(directory,"coverage.json");
        Storage::CopyFileAtomic(Storage::Join(argv[1],"roboto-regular.ttf"),regular);
        Storage::CopyFileAtomic(Storage::Join(argv[1],"roboto-medium.ttf"),medium);
        Storage::CopyFileAtomic(Storage::Join(argv[1],"roboto-coverage.json"),coverage);
        ui.LoadSmoothFonts(regular,medium,coverage);
        Resize(window,{1280,720});
        auto native=[&](Ui::ThemeId theme){
            ui.Configure(theme,100);ui.ClearFocus();ui.Begin(window,{});const auto& colors=ui.Colors();
            sfRenderWindow_clear(window,colors.background);ui.Rounded({20,20,1240,92},colors.panel,colors.border);
            ui.Wordmark({42,40,180,40});ui.Text(Ui::Theme(theme).name,252,45,26,colors.text);
            ui.Text(u8"Café · Ελληνικά · Кириллица · 日本語 · 😀",40,132,18,colors.muted,1180);
            ui.Card("first",u8"A smooth library card 日本語",{40,184,230,300},nullptr,true);
            ui.Card("second","Unavailable copy",{294,184,230,300},nullptr,false,false);
            ui.Button("normal","Open game",{570,210,280,46});
            ui.Button("accent","Save & apply",{570,278,280,46},true,Ui::Tone::Accent,16);
            ui.Button("disabled","Unavailable",{570,346,280,46},false);
            ui.Button("danger","Delete from library",{570,414,280,46},true,Ui::Tone::Danger,16);
            ui.Wrap("Text is rasterized at the display pixel size. The same actions, exact file names and controller navigation work in every theme.",{900,210,310,180},18,colors.text);
            ui.Finish();sfRenderWindow_display(window);
        };
        native(Ui::ThemeId::RetronixHd);Capture(window,Storage::Join(argv[2],"retronix-hd-ui.png"));
        native(Ui::ThemeId::Modern);Capture(window,Storage::Join(argv[2],"modern-ui.png"));
        const auto badCoverage=Storage::Join(directory,"bad-coverage.json");Storage::AtomicWrite(badCoverage,{'{','}'});
        Fails([&]{ui.LoadSmoothFonts(regular,medium,badCoverage);});
        Need(ui.Context().theme==Ui::ThemeId::Modern,"Failed staged font load changed the working theme");
        native(Ui::ThemeId::Modern);
        Fails([&]{ui.Configure(static_cast<Ui::ThemeId>(999));});
        Fails([&]{ui.Configure(Ui::ThemeId::Modern,0);});
        auto scroll=[&](const Input::UiActions& action,const Ui::Pointer& pointer=Ui::Pointer{}){
            sfRenderWindow_clear(window,ui.Colors().background);ui.Begin(window,action,pointer);
            ui.Text("Clipped controller scroll panel",40,40,24,ui.Colors().text);
            ui.BeginScroll("scroll-fixture",{40,120,500,180},700);
            for(unsigned i=0;i<12;++i)ui.Button("row:"+std::to_string(i),"Scrollable row "+std::to_string(i),{60,130+float(i)*50,440,40});
            ui.EndScroll();ui.Button("outside","Fixed action",{700,160,280,44});
            const auto selected=ui.Finish();sfRenderWindow_display(window);return selected;
        };
        ui.ClearFocus();scroll({});Need(ui.Focus()=="row:0","Scroll panel did not focus its first row");
        for(unsigned i=0;i<10;++i){actions={};actions.down=true;scroll(actions);scroll({});}
        Need(ui.Focus()=="row:10","Controller could not reach offscreen scroll controls");
        const auto visible=Control(ui,"row:10");
        Need(visible.top>=120&&visible.top+visible.height<=300,"Focused offscreen control was not scrolled into its viewport");
        Need(scroll({},Ui::Pointer{80,80,true,true}).empty(),"Clipped scroll control accepted a pointer hit outside its viewport");
        Need(scroll({},Ui::Pointer{80,visible.top+20,true,true})=="row:10","Scrolled pointer hit used unshifted content coordinates");
        Capture(window,Storage::Join(argv[2],"modern-scroll-ui.png"));
        scroll({},Ui::Pointer{80,200,false,false,100});
        Need(std::abs(Control(ui,"row:0").top-130)<0.1,"Wheel scrolling did not return the panel to its start");
        scroll({},Ui::Pointer{1000,600,false,false,-100});
        Need(std::abs(Control(ui,"row:0").top-130)<0.1,"Wheel outside a scroll viewport moved its contents");
        auto twoPanels=[&](const Input::UiActions& action,const Ui::Pointer& pointer=Ui::Pointer{}){
            sfRenderWindow_clear(window,ui.Colors().background);ui.Begin(window,action,pointer);
            ui.BeginScroll("left-panel",{20,120,250,200},600);
            for(unsigned i=0;i<10;++i)ui.Button("left:"+std::to_string(i),"Category "+std::to_string(i),{40,130+float(i)*45,210,36});
            ui.EndScroll();ui.BeginScroll("right-panel",{320,120,500,200},600);
            for(unsigned i=0;i<10;++i)ui.Button("right:"+std::to_string(i),"Setting "+std::to_string(i),{340,130+float(i)*45,450,36});
            ui.EndScroll();ui.Button("fixed-bottom","Save settings",{340,600,300,44});
            const auto selected=ui.Finish();sfRenderWindow_display(window);return selected;
        };
        ui.ClearFocus();twoPanels({});
        twoPanels({},Ui::Pointer{60,180,false,false,-6});const auto leftTop=Control(ui,"left:0").top;
        Need(leftTop<130&&std::abs(Control(ui,"right:0").top-130)<0.1,"Scrolling the sidebar moved the body panel");
        twoPanels({},Ui::Pointer{360,180,false,false,-4});
        Need(std::abs(Control(ui,"left:0").top-leftTop)<0.1&&Control(ui,"right:0").top<130,"Scrolling the body changed the sidebar offset");
        ui.Focus("left:8");twoPanels({});twoPanels({});actions={};actions.right=true;twoPanels(actions);twoPanels({});
        Need(ui.Focus().compare(0,6,"right:")==0&&std::abs(Control(ui,"left:0").top-leftTop)<0.1,"D-pad could not cross independently scrolling panels");
        const auto focused=Control(ui,ui.Focus());
        Need(focused.top>=120&&focused.top+focused.height<=320,"Cross-panel focus remained clipped offscreen");
        Need(std::abs(Control(ui,"fixed-bottom").top-600)<0.1,"Scroll panel shifted the fixed action bar");
        Capture(window,Storage::Join(argv[2],"modern-two-panels-ui.png"));
        const sfVector2u resolutions[]={{640,360},{1280,720},{1920,1080},{800,800},{2560,1080},{3840,2160}};
        const unsigned scales[]={200,125,150,125,100,200};
        for(unsigned i=0;i<6;++i){
            Resize(window,resolutions[i]);
            ui.Configure(Ui::ThemeId::Modern,scales[i]);ui.Begin(window,{});const auto context=ui.Context();
            Need(context.width>=639.9f&&context.height>=359.9f&&context.requestedScale==scales[i],"Small-window UI scale lost the minimum canvas or requested preference");
            const auto center=ui.ToLogical({int(resolutions[i].x/2),int(resolutions[i].y/2)});
            if(std::abs(center.x-context.width/2)>=0.1||std::abs(center.y-context.height/2)>=0.1)
                throw std::runtime_error("Native pointer mapping disagrees with display scale at "+std::to_string(resolutions[i].x)+"x"+std::to_string(resolutions[i].y)+
                    ": logical "+std::to_string(context.width)+"x"+std::to_string(context.height)+", pointer "+std::to_string(center.x)+","+std::to_string(center.y));
            sfRenderWindow_clear(window,ui.Colors().background);ui.Wordmark({24,24,180,36});
            ui.Text(u8"Native scale · 日本語 😀",24,90,20,ui.Colors().text,context.width-48);
            const auto grid=Ui::LibraryLayout(context);ui.Card("layout-card",u8"Compact title 日本語",grid.Card(0),nullptr,true);
            ui.Button("edge","Window edge",{context.width-220,context.height-60,190,36});ui.Finish();sfRenderWindow_display(window);
            if(i==0)Capture(window,Storage::Join(argv[2],"modern-compact-card-ui.png"));
        }
        Resize(window,{640,360});ui.Configure(Ui::ThemeId::Modern,100);
        auto denseForm=[&](const Input::UiActions& action){
            sfRenderWindow_clear(window,ui.Colors().background);ui.Begin(window,action);
            ui.BeginScroll("dense-sidebar",{16,80,154,190},310);
            for(unsigned i=0;i<6;++i)ui.Button("tab:"+std::to_string(i),"Section "+std::to_string(i),{16,80+float(i)*48,154,40});
            ui.EndScroll();ui.BeginScroll("dense-body",{198,118,406,150},600);
            for(unsigned i=0;i<12;++i)ui.Button("body:"+std::to_string(i),"Form control "+std::to_string(i),{210,130+float(i)*44,380,36});
            ui.EndScroll();ui.Button("dense-save","Save",{24,290,248,40});ui.Button("dense-cancel","Cancel",{368,290,248,40});
            ui.Finish();sfRenderWindow_display(window);
        };
        ui.Focus("body:0");denseForm({});denseForm({});
        for(unsigned i=1;i<12;++i){actions={};actions.down=true;denseForm(actions);denseForm({});
            Need(ui.Focus()=="body:"+std::to_string(i),"Fixed footer/sidebar stole vertical navigation before the scroll content ended");
            const auto row=Control(ui,ui.Focus());Need(row.top>=118&&row.top+row.height<=268,"Dense-form focus did not scroll fully into view");}
        bool panelPublished=false;
        for(const auto& item:ui.Controls())if(item.id=="body:11")panelPublished=item.panel=="dense-body"&&item.clip.left==198&&item.clip.height==150;
        Need(panelPublished,"Public control metadata lost its scroll navigation group or clipping bounds");
        actions={};actions.down=true;denseForm(actions);
        Need(ui.Focus().compare(0,5,"body:")!=0,"Vertical navigation could not leave the panel at its actual end");
        Capture(window,Storage::Join(argv[2],"modern-dense-form-ui.png"));
        ui.Configure(Ui::ThemeId::Retronix,200);Resize(window,{800,800});draw({});
        Need(ui.Context().theme==Ui::ThemeId::Retronix&&std::abs(ui.ToLogical({400,400}).x-320)<0.1,"Returning to Retronix changed legacy coordinates");
        std::cout<<"Retronix/HD/Modern rendering, staged fonts, Unicode fallback, spatial focus, clipped scrolling, pointer mapping and scale matrix passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<"UI test: "<<error.what()<<'\n';return 1;}
}
