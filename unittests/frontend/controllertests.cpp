#include "../../platform/controller.hpp"
#include "../../genesis/gamepad/gamepad.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void Need(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class Function> void Invalid(Function function, const char* message) {
    try { function(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error(message);
}
void Mapping() {
    Controller::State state;
    state.buttons = 0xffff;
    state.leftX = 32767;
    Need(Controller::MapState(state, 25) == 0, "Disconnected native samples release every button");
    state = {};
    state.connected = true;
    struct Button { unsigned native; unsigned mega; };
    const Button buttons[] = {
        {Controller::DpadUp, 0x001}, {Controller::DpadDown, 0x002},
        {Controller::DpadLeft, 0x004}, {Controller::DpadRight, 0x008},
        {Controller::A, 0x010}, {Controller::B, 0x020}, {Controller::X, 0x040},
        {Controller::Start, 0x080}, {Controller::Y, 0x100},
        {Controller::LeftShoulder, 0x200}, {Controller::RightShoulder, 0x400},
        {Controller::Back, 0x800}
    };
    for (const auto& button : buttons) {
        state.buttons = std::uint16_t(button.native);
        Need(Controller::MapState(state, 25) == button.mega, "Xbox button maps to the correct Mega Drive wire");
    }
    state.buttons = 0x00c0; // Stick clicks are not Mega Drive buttons.
    Need(Controller::MapState(state, 25) == 0, "Unmapped native buttons remain inactive");
    state.buttons = 0;
    const int threshold = 8191; // floor(32767 * 25 / 100).
    state.leftX = std::int16_t(threshold);
    state.leftY = std::int16_t(-threshold);
    Need(Controller::MapState(state, 25) == 0, "Stick deadzone includes its exact boundary");
    ++state.leftX; --state.leftY;
    Need(Controller::MapState(state, 25) == 0x00a, "Stick diagonals use XInput positive-up orientation");
    state.leftX = -32768; state.leftY = 32767;
    Need(Controller::MapState(state, 25) == 0x005, "Full native axis range handles negative minimum");
    state.buttons = Controller::DpadRight | Controller::DpadDown;
    Need(Controller::MapState(state, 25) == 0, "Opposite stick and D-pad directions neutralize");
    state.leftX = state.leftY = 0;
    state.buttons = Controller::DpadLeft | Controller::DpadRight | Controller::DpadUp | Controller::DpadDown;
    Need(Controller::MapState(state, 25) == 0, "Contradictory D-pad samples neutralize");
    Invalid([&] { Controller::MapState(state, 0); }, "Zero deadzone rejected");
    Invalid([&] { Controller::MapState(state, 96); }, "Excessive deadzone rejected");

    Controller::JoystickState joystick;
    joystick.buttons[0] = true;
    joystick.leftY = -100.f;
    Need(!Controller::FromJoystick(joystick).connected, "Disconnected Linux sample cannot press stale controls");
    joystick.connected = true;
    const unsigned expected[] = {0x010, 0x020, 0x040, 0x100, 0x200, 0x400, 0x800, 0x080};
    joystick.leftY = 0;
    for (unsigned i = 0; i < 8; ++i) {
        joystick.buttons.fill(false);
        joystick.buttons[i] = true;
        Need(Controller::MapState(Controller::FromJoystick(joystick), 25) == expected[i], "Linux Xbox button order matches native XInput mapping");
    }
    joystick.buttons.fill(false);
    joystick.povX = -100; joystick.povY = -100;
    Need(Controller::MapState(Controller::FromJoystick(joystick), 25) == 0x005, "Linux hat up is negative Y");
    joystick.povX = joystick.povY = 0;
    joystick.leftX = 500; joystick.leftY = 500;
    Need(Controller::MapState(Controller::FromJoystick(joystick), 25) == 0x00a, "Linux stick axes are bounded and Y is reversed");
    joystick.leftX = std::numeric_limits<float>::quiet_NaN();
    joystick.leftY = std::numeric_limits<float>::infinity();
    joystick.povX = joystick.povY = std::numeric_limits<float>::infinity();
    Need(Controller::MapState(Controller::FromJoystick(joystick), 25) == 0, "Non-finite stick samples cannot press controls");
}
void DevicesAndOptions() {
    Controller::Devices devices{};
    devices[0].connected = true; devices[0].buttons = Controller::A;
    devices[3].connected = true; devices[3].buttons = Controller::Start;
    Controller::Options options;
    auto pads = Controller::MapDevices(devices, options, true);
    Need(pads[0] == 0x010 && pads[1] == 0, "Default ports use native slots zero and one");
    options.player1 = 3; options.player2 = 0;
    pads = Controller::MapDevices(devices, options, true);
    Need(pads[0] == 0x080 && pads[1] == 0x010, "Configured native slots retain player identity");
    Need(Controller::MapDevices(devices, options, false) == Controller::Pads{}, "Focus loss releases both controller ports");
    options.enabled = false;
    Need(Controller::MapDevices(devices, options, true) == Controller::Pads{}, "Disabling controller input releases both ports");
    options.enabled = true; options.player2 = -1;
    pads = Controller::MapDevices(devices, options, true);
    Need(pads[0] == 0x080 && pads[1] == 0, "Minus one disables an individual port");
    devices[3].connected = false;
    Need(Controller::MapDevices(devices, options, true) == Controller::Pads{}, "Unplugging releases state despite stale native packet");
    devices[3].connected = true;
    Need(Controller::MapDevices(devices, options, true)[0] == 0x080, "Reconnected slot becomes active again");
    Controller::Configure(options);
    auto invalid = options; invalid.player1 = 8;
    Invalid([&] { Controller::Configure(invalid); }, "Out-of-range device index rejected");
    invalid = options; invalid.player2 = -2;
    Invalid([&] { Controller::Configure(invalid); }, "Invalid disabled index rejected");
    invalid = options; invalid.deadzone = 0;
    Invalid([&] { Controller::Configure(invalid); }, "Invalid controller options rejected");
    Need(Controller::GetOptions().player1 == 3 && Controller::GetOptions().deadzone == 25, "Configure failure preserves previous options");
    Controller::Configure({});
}
void SourceMixingAndBus() {
    GamePad::Init();
    GamePad::SetButton(0, GamePad::MD_B, true);
    GamePad::SetControllerState(0, 0x010);
    GamePad::SetButton(0, GamePad::MD_B, false);
    Need(!(GamePad::ReadData(0) & 0x10), "Keyboard release does not release a held physical button");
    GamePad::SetButton(0, GamePad::MD_B, true);
    GamePad::SetControllerState(0, 0);
    Need(!(GamePad::ReadData(0) & 0x10), "Physical release does not release a held keyboard button");
    GamePad::SetControllerState(1, 0x020);
    Need(!(GamePad::ReadData(1) & 0x20) && (GamePad::ReadData(1) & 0x10), "Player two remains isolated from player one");
    GamePad::SetFocus(false);
    Need(GamePad::ReadData(0) == 0x7f && GamePad::ReadData(1) == 0x7f, "Focus loss clears keyboard and physical controls");
    GamePad::SetButton(0, GamePad::MD_B, true);
    GamePad::SetControllerState(1, 0x020);
    Need(GamePad::ReadData(0) == 0x7f && GamePad::ReadData(1) == 0x7f, "Unfocused input stays released");
    GamePad::SetFocus(true);
    Need(GamePad::ReadData(0) == 0x7f && GamePad::ReadData(1) == 0x7f, "Focus gain cannot resurrect stale keyboard input");
    GamePad::SetControllerState(0, 0xf00);
    GamePad::Write(0, 0x40, true);
    GamePad::Write(0, 0x40, false);
    for (unsigned i = 0; i < 3; ++i) { GamePad::Write(0, 0, false); GamePad::Write(0, 0x40, false); }
    Need(GamePad::ReadData(0) == 0x70, "Physical XYZ and MODE survive the six-button bus handshake");
    GamePad::SetControllerState(0, 0);
    Need(GamePad::ReadData(0) == 0x7f, "Unplug releases XYZ and MODE in the extended phase");
    GamePad::Init();
    GamePad::SetControllerState(0, 0xffff);
    Need(GamePad::ReadData(0) == 0x40, "Native masks only drive the twelve controller buttons");
    GamePad::Init();
    Need(GamePad::ReadData(0) == 0x7f && GamePad::ReadData(1) == 0x7f, "Reset clears all sources");
}
}

int main() {
    try {
        Mapping(); DevicesAndOptions(); SourceMixingAndBus();
        std::cout << "Controller mappings, deadzone, device selection, reconnect, source mixing, focus and six-button bus passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
