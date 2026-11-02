#include "../../frontend/ui.hpp"
#include "../../platform/storage.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void Need(bool okay,const char* reason){if(!okay)throw std::runtime_error(reason);}
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
        sfRenderWindow_setSize(window,{800,800});sfEvent event;while(sfRenderWindow_pollEvent(window,&event)){}
        draw({});const auto logical=ui.ToLogical({400,400});
        Need(std::abs(logical.x-320)<0.1&&std::abs(logical.y-180)<0.1,"Resized UI pointer transform lost letterboxing");
        std::cout<<"Retro UI spatial focus, exact clicks, Unicode fonts, long-title clipping and resize checks passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<"UI test: "<<error.what()<<'\n';return 1;}
}
