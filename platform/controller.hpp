#ifndef DRIVONIX_CONTROLLER_HPP
#define DRIVONIX_CONTROLLER_HPP

#include <array>
#include <cstdint>

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
    Start = 0x0010, Back = 0x0020,
    LeftShoulder = 0x0100, RightShoulder = 0x0200,
    A = 0x1000, B = 0x2000, X = 0x4000, Y = 0x8000
};

struct State {
    bool connected = false;
    std::uint16_t buttons = 0;
    std::int16_t leftX = 0; // -32768..32767; positive is right.
    std::int16_t leftY = 0; // Positive is up, matching XInput.
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

}
#endif
