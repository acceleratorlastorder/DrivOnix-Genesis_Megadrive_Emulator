#ifndef DRIVONIX_FRONTEND_INPUT_HPP
#define DRIVONIX_FRONTEND_INPUT_HPP

#include "../platform/controller.hpp"
#include <array>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace Settings { struct Values; }
namespace Input {

constexpr unsigned ActionCount = 12;
enum Action : unsigned { Up, Down, Left, Right, B, C, A, Start, X, Y, Z, Mode };
enum class Device { Keyboard, Controller };
struct Profile {
    // Comma-separated symbolic alternatives, or "none". Native numeric key
    // codes are never persisted. Array order matches the Mega Drive bus bits.
    std::array<std::string, ActionCount> keyboard;
    std::array<std::string, ActionCount> controller;
};
using Profiles = std::array<Profile, 2>;
Profiles DefaultProfiles();
const char* ActionName(unsigned action);
std::string KeyName(int nativeKeyCode); // CSFML 2 logical key code; no SDK dependency.
int KeyCode(const std::string& name);
std::string NormalizeBinding(const std::string& value, Device device);
std::vector<std::string> BindingTokens(const std::string& value);
void ValidateProfiles(const Profiles& profiles);

struct Frame {
    std::set<std::string> keys;
    Controller::Devices devices{};
    bool focused = true;
};
std::set<std::string> ControllerSymbols(const Controller::State& state, unsigned deadzone = 25);
bool Neutral(const Frame& frame, unsigned deadzone = 25);
Controller::Pads MapGame(const Frame& frame, const Settings::Values& settings);

struct UiActions {
    bool up=false, down=false, left=false, right=false;
    bool confirm=false, back=false, menu=false, fullscreen=false;
    bool search=false, pageUp=false, pageDown=false;
};
struct Routed { Controller::Pads game{}; UiActions ui; };
class Router {
public:
    Routed Update(const Frame& frame, const Settings::Values& settings, bool playing, std::uint64_t nowMs);
    void SuppressUntilNeutral();
    void Reset();
private:
    std::set<std::string> previous_;
    std::array<std::uint64_t,4> repeatAt_{};
    bool suppressed_=false;
};

enum class CaptureStatus { WaitingForNeutral, Listening, Captured, Cancelled, Disconnected };
struct CaptureResult { CaptureStatus status=CaptureStatus::WaitingForNeutral; std::string binding; };
class Capture {
public:
    void Begin(Device device, int controllerIndex = 0);
    CaptureResult Update(const Frame& frame, std::uint64_t nowMs);
    void Cancel();
private:
    Device device_=Device::Keyboard;
    int index_=0;
    CaptureResult result_;
};
std::vector<unsigned> Conflicts(const Profile& profile, Device device, unsigned action, const std::string& binding);
// Swap one conflict or reject multiple conflicts without changing the profile.
bool Assign(Profile& profile, Device device, unsigned action, const std::string& binding, bool swap,
            std::string* error = nullptr);

}
#endif
