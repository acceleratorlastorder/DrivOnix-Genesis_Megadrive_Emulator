#include "controller.hpp"
#include "../genesis/gamepad/gamepad.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

#ifndef DRIVONIX_HEADLESS
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <xinput.h>
#include <string>
#else
#include <SFML/Window/Joystick.h>
#endif
#endif

namespace Controller {
namespace {
Options currentOptions;
constexpr std::uint16_t Bit(GamePad::MD_BUTTON button) {
    return std::uint16_t(1u << unsigned(button));
}
std::int16_t Axis(float value, bool reverse) {
    if (!std::isfinite(value)) return 0;
    value = std::max(-100.f, std::min(100.f, value));
    const auto sample = std::lround(value * (32767.f / 100.f));
    return std::int16_t(reverse ? -sample : sample);
}

#ifndef DRIVONIX_HEADLESS
#ifdef _WIN32
class XInputApi {
public:
    using GetState = DWORD (WINAPI *)(DWORD, XINPUT_STATE*);
    XInputApi() {
        // An absolute system path prevents loading a game-adjacent DLL with the
        // same name. All supported Windows versions supply at least 9_1_0.
        wchar_t directory[32768];
        const UINT length = GetSystemDirectoryW(directory, 32768);
        if (!length || length >= 32768) return;
        const std::wstring prefix = std::wstring(directory, length) + L"\\";
        for (const wchar_t* name : {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"}) {
            module_ = LoadLibraryW((prefix + name).c_str());
            if (!module_) continue;
            getState = reinterpret_cast<GetState>(GetProcAddress(module_, "XInputGetState"));
            if (getState) break;
            FreeLibrary(module_);
            module_ = nullptr;
        }
    }
    ~XInputApi() { if (module_) FreeLibrary(module_); }
    XInputApi(const XInputApi&) = delete;
    XInputApi& operator=(const XInputApi&) = delete;
    GetState getState = nullptr;
private:
    HMODULE module_ = nullptr;
};
State ReadDevice(unsigned device) {
    static XInputApi api;
    State result;
    if (device >= XUSER_MAX_COUNT || !api.getState) return result;
    XINPUT_STATE state{};
    if (api.getState(device, &state) != ERROR_SUCCESS) return result;
    result.connected = true;
    result.buttons = state.Gamepad.wButtons;
    result.leftX = state.Gamepad.sThumbLX;
    result.leftY = state.Gamepad.sThumbLY;
    return result;
}
#else
State ReadDevice(unsigned device) {
    JoystickState state;
    if (device >= sfJoystickCount || !sfJoystick_isConnected(device)) return {};
    state.connected = true;
    const unsigned count = std::min<unsigned>(sfJoystick_getButtonCount(device), unsigned(state.buttons.size()));
    for (unsigned button = 0; button < count; ++button)
        state.buttons[button] = sfJoystick_isButtonPressed(device, button) != sfFalse;
    const auto axis = [device](sfJoystickAxis which) {
        return sfJoystick_hasAxis(device, which) ? sfJoystick_getAxisPosition(device, which) : 0.f;
    };
    state.leftX = axis(sfJoystickX);
    state.leftY = axis(sfJoystickY);
    state.povX = axis(sfJoystickPovX);
    state.povY = axis(sfJoystickPovY);
    return FromJoystick(state);
}
#endif
#endif
}

void Validate(const Options& options) {
    if (options.deadzone < 1 || options.deadzone > 95)
        throw std::invalid_argument("Controller deadzone must be from 1 through 95 percent");
    if (options.player1 < -1 || options.player1 > 7 || options.player2 < -1 || options.player2 > 7)
        throw std::invalid_argument("Controller device index must be -1 or from 0 through 7");
}
void Configure(const Options& options) {
    Validate(options);
    currentOptions = options;
}
Options GetOptions() { return currentOptions; }

State FromJoystick(const JoystickState& source) {
    State state;
    if (!source.connected) return state;
    state.connected = true;
    const NativeButton buttons[] = {A, B, X, Y, LeftShoulder, RightShoulder, Back, Start};
    for (unsigned button = 0; button < 8; ++button)
        if (source.buttons[button]) state.buttons |= buttons[button];
    if (std::isfinite(source.povX) && source.povX < -50.f) state.buttons |= DpadLeft;
    if (std::isfinite(source.povX) && source.povX > 50.f) state.buttons |= DpadRight;
    if (std::isfinite(source.povY) && source.povY < -50.f) state.buttons |= DpadUp;
    if (std::isfinite(source.povY) && source.povY > 50.f) state.buttons |= DpadDown;
    state.leftX = Axis(source.leftX, false);
    state.leftY = Axis(source.leftY, true);
    return state;
}

std::uint16_t MapState(const State& state, unsigned deadzone) {
    if (deadzone < 1 || deadzone > 95)
        throw std::invalid_argument("Controller deadzone must be from 1 through 95 percent");
    if (!state.connected) return 0;
    const int threshold = int(32767u * deadzone / 100u);
    std::uint16_t result = 0;
    const auto held = [&state](NativeButton button) { return (state.buttons & button) != 0; };
    if (held(DpadUp) || int(state.leftY) > threshold) result |= Bit(GamePad::MD_UP);
    if (held(DpadDown) || int(state.leftY) < -threshold) result |= Bit(GamePad::MD_DOWN);
    if (held(DpadLeft) || int(state.leftX) < -threshold) result |= Bit(GamePad::MD_LEFT);
    if (held(DpadRight) || int(state.leftX) > threshold) result |= Bit(GamePad::MD_RIGHT);
    // Resolve conflicting stick/D-pad directions to neutral on that axis.
    const auto horizontal = Bit(GamePad::MD_LEFT) | Bit(GamePad::MD_RIGHT);
    const auto vertical = Bit(GamePad::MD_UP) | Bit(GamePad::MD_DOWN);
    if ((result & horizontal) == horizontal) result &= std::uint16_t(~horizontal);
    if ((result & vertical) == vertical) result &= std::uint16_t(~vertical);
    if (held(X)) result |= Bit(GamePad::MD_A);
    if (held(A)) result |= Bit(GamePad::MD_B);
    if (held(B)) result |= Bit(GamePad::MD_C);
    if (held(Y)) result |= Bit(GamePad::MD_X);
    if (held(LeftShoulder)) result |= Bit(GamePad::MD_Y);
    if (held(RightShoulder)) result |= Bit(GamePad::MD_Z);
    if (held(Start)) result |= Bit(GamePad::MD_START);
    if (held(Back)) result |= Bit(GamePad::MD_MODE);
    return result;
}

Pads MapDevices(const Devices& devices, const Options& options, bool focused) {
    Validate(options);
    Pads result{};
    if (!focused || !options.enabled) return result;
    const int indices[] = {options.player1, options.player2};
    for (unsigned pad = 0; pad < result.size(); ++pad)
        if (indices[pad] >= 0) result[pad] = MapState(devices[unsigned(indices[pad])], options.deadzone);
    return result;
}

Pads Poll(bool focused) {
    const auto options = GetOptions();
    Devices devices{};
#ifndef DRIVONIX_HEADLESS
    if (focused && options.enabled) {
#ifndef _WIN32
        sfJoystick_update();
#endif
        const int indices[] = {options.player1, options.player2};
        for (unsigned pad = 0; pad < 2; ++pad)
            if (indices[pad] >= 0 && (pad == 0 || indices[pad] != indices[0]))
                devices[unsigned(indices[pad])] = ReadDevice(unsigned(indices[pad]));
    }
#endif
    return MapDevices(devices, options, focused);
}
}
