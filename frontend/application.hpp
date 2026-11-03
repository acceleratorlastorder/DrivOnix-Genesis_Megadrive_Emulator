#ifndef DRIVONIX_FRONTEND_APPLICATION_HPP
#define DRIVONIX_FRONTEND_APPLICATION_HPP
#include <string>
#include <functional>
#include "ui.hpp"
namespace Frontend {
// Optional embedding hook; empty in normal desktop launches. It exercises the
// same input router and UI loop as physical devices.
struct AutomationFrame {
    Input::Frame input;
    std::vector<Ui::ControlInfo> controls;
    std::string page,focus,error,screenshotPath;
    bool active=false,paused=true,requestClose=false;
    uint64_t frames=0;
    std::string theme,windowId;unsigned uiScale=100;
    sfVector2u windowSize{0,0},resize{0,0};
    std::vector<sfUint32> text;
};
struct Options {
    std::string configPath,romPath,member;
    std::function<void(AutomationFrame&)> automation;
    bool portable=false,customConfig=false,noAudio=false,noDialogs=false,memberProvided=false;
};
int RunApplication(const Options& options);
}
#endif
