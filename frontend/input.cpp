#include "input.hpp"
#include "../platform/settings.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace Input {
namespace {
const char* const Actions[]={"up","down","left","right","b","c","a","start","x","y","z","mode"};
const char* const Keys[]={
    "a","b","c","d","e","f","g","h","i","j","k","l","m","n","o","p","q","r","s","t","u","v","w","x","y","z",
    "num0","num1","num2","num3","num4","num5","num6","num7","num8","num9",
    "escape","lcontrol","lshift","lalt","lsystem","rcontrol","rshift","ralt","rsystem","menu",
    "lbracket","rbracket","semicolon","comma","period","apostrophe","slash","backslash","grave","equal","hyphen",
    "space","enter","backspace","tab","pageup","pagedown","end","home","insert","delete","add","subtract","multiply","divide",
    "left","right","up","down","numpad0","numpad1","numpad2","numpad3","numpad4","numpad5","numpad6","numpad7","numpad8","numpad9",
    "f1","f2","f3","f4","f5","f6","f7","f8","f9","f10","f11","f12","f13","f14","f15","pause"
};
std::string Lower(std::string value) {
    for(char& c:value) if(c>='A'&&c<='Z')c=char(c-'A'+'a');
    return value;
}
std::string Trim(const std::string& value) {
    const auto first=value.find_first_not_of(" \t\r\n");
    return first==std::string::npos ? std::string():value.substr(first,value.find_last_not_of(" \t\r\n")-first+1);
}
bool Indexed(const std::string& value,const std::string& prefix,unsigned maximum,bool axis=false) {
    if(value.compare(0,prefix.size(),prefix)!=0)return false;
    auto digits=value.substr(prefix.size());
    if(axis) {if(digits.empty()||(digits.back()!='+'&&digits.back()!='-'))return false;digits.pop_back();}
    if(digits.empty()||(digits.size()>1&&digits.front()=='0'))return false;
    unsigned index=0;
    for(char c:digits){if(c<'0'||c>'9'||index>maximum)return false;index=index*10+unsigned(c-'0');}
    return index<=maximum;
}
bool ControllerToken(const std::string& token) {
    const char* const names[]={"a","b","x","y","lb","rb","back","start","l3","r3",
        "dpad_up","dpad_down","dpad_left","dpad_right","left_x+","left_x-","left_y+","left_y-",
        "right_x+","right_x-","right_y+","right_y-","left_trigger","right_trigger"};
    for(const auto* name:names)if(token==name)return true;
    return Indexed(token,"button_",31)||Indexed(token,"axis_",7,true);
}
bool Reserved(const std::string& token,Device device) {
    return device==Device::Keyboard ? token=="escape"||token=="f1"||token=="f11":token=="r3"||token=="button_10";
}
bool Held(const std::string& binding,const std::set<std::string>& symbols) {
    for(const auto& token:BindingTokens(binding))if(token!="none"&&symbols.count(token))return true;
    return false;
}
bool Intersects(const std::string& a,const std::string& b) {
    const auto first=BindingTokens(a),second=BindingTokens(b);
    for(const auto& token:first)if(token!="none"&&std::find(second.begin(),second.end(),token)!=second.end())return true;
    return false;
}
std::array<std::string,ActionCount>& Bindings(Profile& p,Device d){return d==Device::Keyboard?p.keyboard:p.controller;}
const std::array<std::string,ActionCount>& Bindings(const Profile& p,Device d){return d==Device::Keyboard?p.keyboard:p.controller;}
}

Profiles DefaultProfiles() {
    Profiles result;
    result[0].keyboard={{"z","s","q","d","l","m","k","a","i","o","p","enter"}};
    result[1].keyboard={{"up","down","left","right","numpad2","numpad3","numpad1","numpad4","numpad7","numpad8","numpad9","numpad0"}};
    for(auto& profile:result)profile.controller={{
        "dpad_up,left_y+","dpad_down,left_y-","dpad_left,left_x-","dpad_right,left_x+",
        "a","b","x","start","y","lb","rb","back"}};
    return result;
}
const char* ActionName(unsigned action){return action<ActionCount?Actions[action]:"";}
std::string KeyName(int code){return code>=0&&unsigned(code)<sizeof(Keys)/sizeof(Keys[0])?Keys[code]:"";}
int KeyCode(const std::string& name) {
    const auto key=Lower(name)=="return"?std::string("enter"):Lower(name);
    for(unsigned i=0;i<sizeof(Keys)/sizeof(Keys[0]);++i)if(key==Keys[i])return int(i);
    return -1;
}
std::vector<std::string> BindingTokens(const std::string& value) {
    std::vector<std::string> result;std::size_t start=0;
    do {const auto end=value.find(',',start);auto token=Lower(Trim(value.substr(start,end==std::string::npos?end:end-start)));
        if(token=="return")token="enter";
        result.push_back(std::move(token));
        if(end==std::string::npos)break;
        start=end+1;}while(start<=value.size());
    return result;
}
std::string NormalizeBinding(const std::string& value,Device device) {
    const auto tokens=BindingTokens(value);std::set<std::string> seen;std::string result;
    if(tokens.size()>8)throw std::invalid_argument("A binding may contain at most eight alternatives");
    for(auto token:tokens) {
        if(token=="return")token="enter";
        if(token.empty()||(token=="none"&&tokens.size()!=1))throw std::invalid_argument("Use a symbolic binding or none");
        if(token!="none"&&((device==Device::Keyboard&&KeyCode(token)<0)||(device==Device::Controller&&!ControllerToken(token))))
            throw std::invalid_argument("Unknown input binding: "+token);
        if(Reserved(token,device))throw std::invalid_argument("Input is reserved for the frontend: "+token);
        if(!seen.insert(token).second)throw std::invalid_argument("Duplicate binding alternative: "+token);
        if(!result.empty())result+=',';
        result+=token;
    }
    return result;
}
void ValidateProfiles(const Profiles& profiles) {
    for(const auto& profile:profiles)for(unsigned action=0;action<ActionCount;++action) {
        NormalizeBinding(profile.keyboard[action],Device::Keyboard);
        NormalizeBinding(profile.controller[action],Device::Controller);
    }
}
std::set<std::string> ControllerSymbols(const Controller::State& state,unsigned deadzone) {
    std::set<std::string> result;if(!state.connected)return result;
    deadzone=std::min(95u,std::max(1u,deadzone));
    const auto button=[&](unsigned bit,const char* name){if(state.buttons&bit)result.insert(name);};
    button(Controller::A,"a");button(Controller::B,"b");button(Controller::X,"x");button(Controller::Y,"y");
    button(Controller::LeftShoulder,"lb");button(Controller::RightShoulder,"rb");
    button(Controller::Start,"start");button(Controller::Back,"back");
    button(Controller::LeftThumb,"l3");button(Controller::RightThumb,"r3");
    button(Controller::DpadUp,"dpad_up");button(Controller::DpadDown,"dpad_down");
    button(Controller::DpadLeft,"dpad_left");button(Controller::DpadRight,"dpad_right");
    const int threshold=int(32767u*std::min(95u,std::max(1u,deadzone))/100u);
    const auto axis=[&](int value,const char* name){if(value>threshold)result.insert(std::string(name)+"+");if(value<-threshold)result.insert(std::string(name)+"-");};
    axis(state.leftX,"left_x");axis(state.leftY,"left_y");axis(state.rightX,"right_x");axis(state.rightY,"right_y");
    if(unsigned(state.leftTrigger)*100>255u*deadzone)result.insert("left_trigger");
    if(unsigned(state.rightTrigger)*100>255u*deadzone)result.insert("right_trigger");
    for(unsigned i=0;i<32;++i)if(state.rawButtons&(std::uint32_t(1)<<i))result.insert("button_"+std::to_string(i));
    for(unsigned i=0;i<state.axes.size();++i)axis(state.axes[i],("axis_"+std::to_string(i)).c_str());
    return result;
}
bool Neutral(const Frame& frame,unsigned deadzone) {
    if(!frame.keys.empty())return false;
    for(const auto& device:frame.devices)if(!ControllerSymbols(device,deadzone).empty())return false;
    return true;
}
Controller::Pads MapGame(const Frame& frame,const Settings::Values& settings) {
    Controller::Pads result{};if(!frame.focused)return result;
    const int indices[]={settings.controller1,settings.controller2};
    for(unsigned player=0;player<2;++player) {
        std::set<std::string> symbols;
        if(settings.controllersEnabled&&indices[player]>=0&&indices[player]<8)
            symbols=ControllerSymbols(frame.devices[unsigned(indices[player])],settings.controllerDeadzone);
        for(unsigned action=0;action<ActionCount;++action)
            if(Held(settings.bindings[player].keyboard[action],frame.keys)||Held(settings.bindings[player].controller[action],symbols))
                result[player]|=std::uint16_t(1u<<action);
        if((result[player]&3)==3)result[player]&=std::uint16_t(~3u);
        if((result[player]&12)==12)result[player]&=std::uint16_t(~12u);
    }
    return result;
}
void Router::Reset(){previous_.clear();repeatAt_.fill(0);suppressed_=false;}
void Router::SuppressUntilNeutral(){Reset();suppressed_=true;}
Routed Router::Update(const Frame& frame,const Settings::Values& settings,bool playing,std::uint64_t nowMs) {
    Routed result;
    if(!frame.focused){SuppressUntilNeutral();return result;}
    if(suppressed_){if(Neutral(frame,settings.controllerDeadzone)){suppressed_=false;previous_.clear();}return result;}
    std::set<std::string> pads;
    for(const auto& device:frame.devices) {const auto symbols=ControllerSymbols(device,std::max(50u,settings.controllerDeadzone));pads.insert(symbols.begin(),symbols.end());}
    const auto key=[&](const char* name){return frame.keys.count(name)!=0;};
    const auto pad=[&](const char* name){return pads.count(name)!=0;};
    std::set<std::string> held;
    const auto add=[&](const char* name,bool active){if(active)held.insert(name);};
    add("up",key("up")||pad("dpad_up")||pad("left_y+"));
    add("down",key("down")||pad("dpad_down")||pad("left_y-"));
    add("left",key("left")||pad("dpad_left")||pad("left_x-"));
    add("right",key("right")||pad("dpad_right")||pad("left_x+"));
    add("confirm",key("enter")||pad("a"));
    add("back",key("escape")||pad("b"));
    add("menu",key("f1")||pad("r3")||(playing&&key("escape")));
    add("fullscreen",key("f11"));
    add("search",key("slash")||pad("y"));
    add("pageup",key("pageup")||pad("lb"));
    add("pagedown",key("pagedown")||pad("rb"));
    if(held.count("up")&&held.count("down")){held.erase("up");held.erase("down");}
    if(held.count("left")&&held.count("right")){held.erase("left");held.erase("right");}
    const auto edge=[&](const char* name){return held.count(name)&&!previous_.count(name);};
    result.ui.menu=edge("menu");result.ui.fullscreen=edge("fullscreen");
    if(!playing) {
        bool* directions[]={&result.ui.up,&result.ui.down,&result.ui.left,&result.ui.right};
        const char* names[]={"up","down","left","right"};
        for(unsigned i=0;i<4;++i) {
            if(!held.count(names[i]))repeatAt_[i]=0;
            else if(edge(names[i])){*directions[i]=true;repeatAt_[i]=nowMs+350;}
            else if(nowMs>=repeatAt_[i]){*directions[i]=true;repeatAt_[i]=nowMs+100;}
        }
        result.ui.confirm=edge("confirm");result.ui.back=edge("back");result.ui.search=edge("search");
        result.ui.pageUp=edge("pageup");result.ui.pageDown=edge("pagedown");
    } else if(!result.ui.menu)result.game=MapGame(frame,settings);
    previous_=std::move(held);return result;
}

void Capture::Begin(Device device,int controllerIndex) {
    if(controllerIndex<-1||controllerIndex>7)throw std::invalid_argument("Invalid capture controller index");
    device_=device;index_=controllerIndex;result_={};
}
void Capture::Cancel(){result_.status=CaptureStatus::Cancelled;result_.binding.clear();}
CaptureResult Capture::Update(const Frame& frame,std::uint64_t nowMs) {
    (void)nowMs;
    if(result_.status==CaptureStatus::Captured||result_.status==CaptureStatus::Cancelled||result_.status==CaptureStatus::Disconnected)return result_;
    if(!frame.focused||frame.keys.count("escape")||frame.keys.count("f1")){Cancel();return result_;}
    if(device_==Device::Keyboard) {
        if(result_.status==CaptureStatus::WaitingForNeutral){if(frame.keys.empty())result_.status=CaptureStatus::Listening;return result_;}
        for(const auto& key:frame.keys)if(KeyCode(key)>=0&&!Reserved(key,Device::Keyboard)){result_.status=CaptureStatus::Captured;result_.binding=key;break;}
    } else {
        if(index_<0||!frame.devices[unsigned(index_)].connected){result_.status=CaptureStatus::Disconnected;return result_;}
        const auto symbols=ControllerSymbols(frame.devices[unsigned(index_)],result_.status==CaptureStatus::WaitingForNeutral?25:60);
        if(symbols.count("r3")){Cancel();return result_;}
        if(result_.status==CaptureStatus::WaitingForNeutral){if(symbols.empty())result_.status=CaptureStatus::Listening;return result_;}
        const char* const preferred[]={"a","b","x","y","lb","rb","back","start","l3","dpad_up","dpad_down","dpad_left","dpad_right",
            "left_x+","left_x-","left_y+","left_y-","right_x+","right_x-","right_y+","right_y-","left_trigger","right_trigger"};
        for(const auto* name:preferred)if(symbols.count(name)){result_.status=CaptureStatus::Captured;result_.binding=name;return result_;}
        for(const auto& name:symbols)if(!Reserved(name,Device::Controller)){result_.status=CaptureStatus::Captured;result_.binding=name;break;}
    }
    return result_;
}
std::vector<unsigned> Conflicts(const Profile& profile,Device device,unsigned action,const std::string& binding) {
    if(action>=ActionCount)throw std::invalid_argument("Invalid input action");
    const auto normalized=NormalizeBinding(binding,device);std::vector<unsigned> result;
    const auto& list=Bindings(profile,device);
    for(unsigned i=0;i<ActionCount;++i)if(i!=action&&Intersects(list[i],normalized))result.push_back(i);
    return result;
}
bool Assign(Profile& profile,Device device,unsigned action,const std::string& binding,bool swap,std::string* error) {
    try {
        const auto normalized=NormalizeBinding(binding,device);
        const auto conflicts=Conflicts(profile,device,action,normalized);
        if(!conflicts.empty()&&(!swap||conflicts.size()!=1))throw std::invalid_argument("Binding conflicts with another action");
        auto next=profile;auto& list=Bindings(next,device);
        if(!conflicts.empty())list[conflicts.front()]=list[action];
        list[action]=normalized;profile=std::move(next);return true;
    } catch(const std::exception& failure){if(error)*error=failure.what();return false;}
}
}
