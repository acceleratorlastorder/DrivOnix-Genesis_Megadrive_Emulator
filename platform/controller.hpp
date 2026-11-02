#ifndef DRIVONIX_CONTROLLER_HPP
#define DRIVONIX_CONTROLLER_HPP

#include <array>
#include <cstdint>
#include <string>

namespace Controller {

struct Options {
    bool enabled = true;
    unsigned deadzone = 25; // Percent of each left-stick axis, 1 through 95.
    int player1 = 0;        // Native device index; -1 disables this port.
    int player2 = 1;
};

// These are the documented XINPUT_GAMEPAD button bits. Linux joystick samples
// are converted to the same representation before applying the common mapping.
enum NativeButton : std::uint16_t {
    DpadUp = 0x0001, DpadDown = 0x0002, DpadLeft = 0x0004, DpadRight = 0x0008,
    Start = 0x0010, Back = 0x0020, LeftThumb = 0x0040, RightThumb = 0x0080,
    LeftShoulder = 0x0100, RightShoulder = 0x0200,
    A = 0x1000, B = 0x2000, X = 0x4000, Y = 0x8000
};

enum class Backend { None, XInput, Joystick };
struct State {
    bool connected = false;
    std::uint16_t buttons = 0;
    std::int16_t leftX = 0; // -32768..32767; positive is right.
    std::int16_t leftY = 0; // Positive is up, matching XInput.
    std::int16_t rightX = 0, rightY = 0;
    std::uint8_t leftTrigger = 0, rightTrigger = 0;
    std::uint32_t rawButtons = 0;
    // Normalized axes: left X/Y, left/right trigger (0..32767), right X/Y,
    // and D-pad X/Y. Positive Y means up on both backends.
    std::array<std::int16_t,8> axes{};
    Backend backend = Backend::None;
    std::string name;
    unsigned vendor = 0, product = 0;
};

// CSFML/Linux uses -100..100 axes and the usual Xbox button order. Keeping the
// conversion independent of CSFML permits exact native-layout regression tests.
struct JoystickState {
    bool connected = false;
    std::array<bool, 32> buttons{};
    float leftX = 0;
    float leftY = 0; // Positive is down on Linux.
    float povX = 0;
    float povY = 0;  // Positive is down on Linux.
    float rightX = 0, rightY = 0;
    float leftTrigger = -100, rightTrigger = -100;
};

using Devices = std::array<State, 8>;
using Pads = std::array<std::uint16_t, 2>;

void Validate(const Options& options);
void Configure(const Options& options);
Options GetOptions();
State FromJoystick(const JoystickState& state);
std::uint16_t MapState(const State& state, unsigned deadzone);
Pads MapDevices(const Devices& devices, const Options& options, bool focused);
Pads Poll(bool focused);
// Raw device polling is independent of enabled gameplay ports and may be used
// by the frontend/library/remapping screens before a cartridge is running.
Devices ReadDevices();
const char* BackendName(Backend backend);

}
#endif
