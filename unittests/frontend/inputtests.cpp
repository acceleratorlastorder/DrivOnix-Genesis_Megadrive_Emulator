#include "../../frontend/input.hpp"
#include "../../platform/settings.hpp"
#include "../../genesis/gamepad/gamepad.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void Need(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
template<class F> void Reject(F function,const char* message){try{function();}catch(const std::exception&){return;}throw std::runtime_error(message);}
void DefaultsAndMapping() {
    Settings::Values settings;Input::Frame frame;
    const char* first[]={"z","s","q","d","l","m","k","a","i","o","p","enter"};
    const char* second[]={"up","down","left","right","numpad2","numpad3","numpad1","numpad4","numpad7","numpad8","numpad9","numpad0"};
    for(unsigned action=0;action<Input::ActionCount;++action) {
        frame.keys={first[action]};auto pads=Input::MapGame(frame,settings);
        Need(pads[0]==(1u<<action)&&pads[1]==0,"Player one default keyboard mapping changed");
        frame.keys={second[action]};pads=Input::MapGame(frame,settings);
        Need(pads[1]==(1u<<action)&&pads[0]==0,"Player two default keyboard mapping changed");
    }
    frame.keys.clear();frame.devices[0].connected=true;
    const unsigned buttons[]={Controller::DpadUp,Controller::DpadDown,Controller::DpadLeft,Controller::DpadRight,
        Controller::A,Controller::B,Controller::X,Controller::Start,Controller::Y,Controller::LeftShoulder,Controller::RightShoulder,Controller::Back};
    for(unsigned action=0;action<Input::ActionCount;++action) {
        frame.devices[0].buttons=std::uint16_t(buttons[action]);
        Need(Input::MapGame(frame,settings)[0]==(1u<<action),"Default Xbox mapping changed");
    }
    frame.devices[0].buttons=0;frame.devices[0].leftY=32767;
    Need(Input::MapGame(frame,settings)[0]==1,"Default movement lost the stick alternative");
    frame.keys={"s"};Need(Input::MapGame(frame,settings)[0]==0,"Opposing independent sources must neutralize");
    frame.keys.clear();frame.devices[0].leftY=0;frame.devices[0].leftTrigger=255;
    settings.bindings[0].controller[Input::A]="left_trigger";
    Need(Input::MapGame(frame,settings)[0]==0x40,"Trigger remapping cannot drive a Mega Drive action");
    settings.controllersEnabled=false;Need(Input::MapGame(frame,settings)[0]==0,"Disabled game controller still drove the core");
    frame.keys={"k"};Need(Input::MapGame(frame,settings)[0]==0x40,"Disabling controller input disabled the keyboard");
    settings.bindings[0].keyboard[Input::Mode]="Return";frame.keys={"enter"};
    Need(Input::MapGame(frame,settings)[0]==0x800,"Accepted Return alias did not match native Enter events");
    frame.focused=false;Need(Input::MapGame(frame,settings)==Controller::Pads{},"Unfocused remapped input reached the core");
    Need(Input::KeyName(58)=="enter"&&Input::KeyCode("Return")==58&&Input::KeyName(-1).empty(),"Symbolic key serialization differs from CSFML2");
}
void Bindings() {
    auto profile=Input::DefaultProfiles()[0];std::string error;
    Need(Input::NormalizeBinding(" W , Up ",Input::Device::Keyboard)=="w,up","Binding alternatives did not normalize");
    Need(!Input::Assign(profile,Input::Device::Keyboard,Input::Up,"s",false,&error)&&profile.keyboard[Input::Up]=="z","Conflict silently overwrote another action");
    Need(Input::Assign(profile,Input::Device::Keyboard,Input::Up,"s",true,&error),"Single conflict could not swap");
    Need(profile.keyboard[Input::Up]=="s"&&profile.keyboard[Input::Down]=="z","Swap lost the previous binding");
    Need(Input::Assign(profile,Input::Device::Keyboard,Input::Up,"none",false,&error),"Clearing a binding failed");
    for(const auto* key:{"escape","f1","f11","unknown","w,W","none,a",""})Reject([&]{Input::NormalizeBinding(key,Input::Device::Keyboard);},"Invalid or reserved keyboard binding accepted");
    for(const auto* key:{"r3","button_10","button_32","axis_8+","garbage"})Reject([&]{Input::NormalizeBinding(key,Input::Device::Controller);},"Invalid or reserved controller binding accepted");
    profile=Input::DefaultProfiles()[0];profile.keyboard[Input::A]="w";profile.keyboard[Input::B]="w";
    Need(!Input::Assign(profile,Input::Device::Keyboard,Input::Up,"w",true,&error)&&profile.keyboard[Input::Up]=="z","Multiple conflicts partially changed profile");
}
void RoutingAndCapture() {
    Settings::Values settings;Input::Frame frame;Input::Router router;
    frame.keys={"enter"};auto routed=router.Update(frame,settings,false,0);
    Need(routed.ui.confirm&&routed.game==Controller::Pads{},"UI confirm leaked into game input");
    Need(!router.Update(frame,settings,false,1).ui.confirm,"Holding confirm repeatedly activated menus");
    router.Reset();frame.keys={"z"};routed=router.Update(frame,settings,true,0);
    Need(routed.game[0]==1&&!routed.ui.confirm&&!routed.ui.up,"Gameplay input activated UI navigation");
    frame.devices[0].connected=true;frame.devices[0].buttons=Controller::RightThumb;
    routed=router.Update(frame,settings,true,1);
    Need(routed.ui.menu&&routed.game==Controller::Pads{},"R3 menu opening leaked input into the same emulated frame");
    Need(!router.Update(frame,settings,true,2).ui.menu,"Holding R3 repeatedly toggled the menu");
    router.SuppressUntilNeutral();frame.devices={};frame.keys={"enter"};
    Need(router.Update(frame,settings,true,3).game==Controller::Pads{},"Held transition confirm reached the game");
    frame.keys.clear();router.Update(frame,settings,true,4);frame.keys={"z"};
    Need(router.Update(frame,settings,true,5).game[0]==1,"Neutral transition gate did not resume input");
    router.Reset();frame.keys={"f11"};
    Need(router.Update(frame,settings,true,0).ui.fullscreen&&!router.Update(frame,settings,true,1).ui.fullscreen,"Fullscreen hotkey was not edge triggered");
    router.Reset();frame.keys={"escape"};
    Need(router.Update(frame,settings,true,0).ui.menu,"Escape no longer opens the pause menu");
    router.Reset();frame.keys={"up"};
    Need(router.Update(frame,settings,false,0).ui.up&&!router.Update(frame,settings,false,349).ui.up&&
        router.Update(frame,settings,false,350).ui.up&&!router.Update(frame,settings,false,449).ui.up&&
        router.Update(frame,settings,false,450).ui.up,"Menu navigation repeat timing changed");
    frame.focused=false;Need(router.Update(frame,settings,false,500).game==Controller::Pads{},"Focus loss emitted game input");
    frame.focused=true;Need(!router.Update(frame,settings,false,501).ui.up,"Focus gain resurrected a held navigation key");
    frame.keys.clear();router.Update(frame,settings,false,502);frame.keys={"up"};
    Need(router.Update(frame,settings,false,503).ui.up,"Fresh post-focus input was suppressed");
    router.Reset();frame.keys={"up","down"};routed=router.Update(frame,settings,false,0);
    Need(!routed.ui.up&&!routed.ui.down,"Contradictory menu directions were not neutralized");

    Input::Capture capture;capture.Begin(Input::Device::Keyboard);
    frame.keys={"enter"};Need(capture.Update(frame,0).status==Input::CaptureStatus::WaitingForNeutral,"Capture bound the button that opened the screen");
    frame.keys.clear();Need(capture.Update(frame,1).status==Input::CaptureStatus::Listening,"Neutral keyboard did not arm capture");
    frame.keys={"k"};auto captured=capture.Update(frame,2);
    Need(captured.status==Input::CaptureStatus::Captured&&captured.binding=="k","Fresh keyboard binding was not captured");
    capture.Begin(Input::Device::Keyboard);frame.keys={"escape"};
    Need(capture.Update(frame,3).status==Input::CaptureStatus::Cancelled,"Escape cannot cancel capture");
    frame.keys.clear();capture.Begin(Input::Device::Controller,0);
    Need(capture.Update(frame,0).status==Input::CaptureStatus::Disconnected,"Disconnected controller capture was not cancelled");
    frame.devices[0].connected=true;frame.devices[0].buttons=Controller::A;capture.Begin(Input::Device::Controller,0);
    Need(capture.Update(frame,0).status==Input::CaptureStatus::WaitingForNeutral,"Held controller confirm was captured");
    frame.devices[0].buttons=0;Need(capture.Update(frame,1).status==Input::CaptureStatus::Listening,"Neutral controller did not arm capture");
    frame.devices[0].leftX=12000;Need(capture.Update(frame,2).status==Input::CaptureStatus::Listening,"Stick noise was captured");
    frame.devices[0].leftX=26000;captured=capture.Update(frame,3);
    Need(captured.status==Input::CaptureStatus::Captured&&captured.binding=="left_x+","Deliberate axis input was not captured symbolically");
    frame.devices[0].leftX=0;frame.devices[0].buttons=Controller::RightThumb;capture.Begin(Input::Device::Controller,0);
    Need(capture.Update(frame,4).status==Input::CaptureStatus::Cancelled,"R3 emergency shortcut became a game binding");
}
void RawAndBus() {
    Controller::JoystickState joystick;joystick.connected=true;
    auto state=Controller::FromJoystick(joystick);Input::Frame frame;frame.devices[0]=state;
    Need(Input::Neutral(frame),"Resting unsigned triggers caused a permanent neutral gate");
    joystick.buttons[9]=joystick.buttons[10]=true;joystick.rightX=100;joystick.rightY=-100;joystick.rightTrigger=100;
    state=Controller::FromJoystick(joystick);
    Need((state.buttons&Controller::RightThumb)&&(state.buttons&Controller::LeftThumb)&&
        (state.rawButtons&(1u<<10))&&state.rightX==32767&&state.rightY==32767&&state.leftTrigger==0&&state.rightTrigger==255,
        "Raw polling lost thumb clicks, right stick, signed orientation or trigger range");
    GamePad::Init();GamePad::SetButton(0,GamePad::MD_UP,true);GamePad::SetControllerState(0,0x10);
    GamePad::SetInputState(0,0x20);
    Need(GamePad::ReadData(0)==0x5f,"Frontend-routed mask retained a stale legacy input source");
    GamePad::SetFocus(false);GamePad::SetInputState(0,0xfff);Need(GamePad::ReadData(0)==0x7f,"Unfocused final mask reached the bus");
}
void DynamicSettings() {
    const std::string base="[display]\nscale=2\nfullscreen=false\n[audio]\nenabled=true\nvolume=40\n[input]\nenabled=true\ndeadzone=25\nplayer1=0\nplayer2=1\n";
    const std::string extra="[interface]\nshow_bios=false\nwindow_width=1366\nwindow_height=768\n[metadata]\nonline=no\n"
        "[library.games]\npath=\"C:\\\\Games; # \\\"日本語\\\"\" ; keep path note\nenabled=yes\nrecursive=no\nfuture=keep\n"
        "[keyboard.player1]\nup=\"W\" ; keep binding note\n[future]\nquoted=\"unknown; # text\"\n";
    auto document=Settings::Document::Parse(base+extra);
    Need(document.Serialize()==base+extra,"Dynamic sections lost comments, casing or unknown text on a no-op save");
    auto values=document.Get();
    Need(!values.showBios&&!values.onlineMetadata&&values.windowWidth==1366&&values.windowHeight==768&&
        values.libraryRoots.size()==1&&values.libraryRoots[0].path==u8"C:\\Games; # \"日本語\""&&
        !values.libraryRoots[0].recursive&&values.bindings[0].keyboard[Input::Up]=="w","Quoted dynamic settings did not decode");
    values.onlineMetadata=true;values.libraryRoots[0].path=u8"/games/été;#日本語";values.bindings[0].keyboard[Input::Up]="e";document.Apply(values);
    const auto changed=document.Serialize();const auto again=Settings::Document::Parse(changed);
    Need(again.Get().libraryRoots[0].path==values.libraryRoots[0].path&&again.Get().onlineMetadata&&
        again.Get().bindings[0].keyboard[Input::Up]=="e","Dynamic settings failed round trip");
    Need(changed.find("; keep path note")!=std::string::npos&&changed.find("future=keep")!=std::string::npos&&
        changed.find("unknown; # text")!=std::string::npos,"Editing known settings removed unrelated text");
    values.libraryRoots.clear();document.Apply(values);const auto removed=document.Serialize();
    Need(Settings::Document::Parse(removed).Get().libraryRoots.empty()&&removed.find("future=keep")!=std::string::npos&&
        removed.find("; keep path note")!=std::string::npos,"Removing a library root lost unrelated comments or retained its active fields");
    values.libraryRoots.push_back({"new-root",u8"/games/新しい",true,true});document.Apply(values);
    Need(Settings::Document::Parse(document.Serialize()).Get().libraryRoots[0].id=="new-root","New dynamic library root was not persisted");
    for(const auto* bad:{"[library.bad.id]\npath=\"x\"\n","[library.games]\npath=\"unterminated\n",
        "[library.games]\npath=\"bad\\q\"\n","[library.games]\nenabled=true\n",
        "[keyboard.player1]\nup=\"escape\"\n","[controller.player1]\na=\"r3\"\n",
        "[library.games]\npath=x\nPATH=y\n","[metadata]\nonline=maybe\n","[interface]\nwindow_width=20\n"})
        Reject([&]{Settings::Document::Parse(bad);},"Invalid dynamic setting was accepted");
    const auto legacy=Settings::Document::Parse("[display]\nscale=4\n");
    Need(!legacy.Get().integerFit&&legacy.Get().windowWidth==1280&&legacy.Get().windowHeight==896,"Legacy scale preference migration changed");
    auto resized=legacy;values=resized.Get();values.windowWidth=1600;resized.Apply(values);
    const auto round=Settings::Document::Parse(resized.Serialize());
    Need(!round.Get().integerFit&&round.Get().windowWidth==1600&&round.Get().windowHeight==896,"Saving window size silently enabled integer fit");
    Settings::Document defaults;const auto fresh=Settings::Document::Parse(defaults.Serialize()).Get();
    Need(fresh.integerFit&&fresh.windowWidth==1280&&fresh.windowHeight==720&&!fresh.showBios&&fresh.onlineMetadata,"New frontend defaults changed");
}
}
int main() {
    try{DefaultsAndMapping();Bindings();RoutingAndCapture();RawAndBus();DynamicSettings();
        std::cout<<"Frontend symbolic bindings, exclusive routing, neutral capture, raw devices and dynamic INI settings passed\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
