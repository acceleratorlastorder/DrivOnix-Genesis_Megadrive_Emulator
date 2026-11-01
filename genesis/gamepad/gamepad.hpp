#ifndef GAMEPAD_HPP
#define GAMEPAD_HPP

#ifndef DRIVONIX_HEADLESS
#include <SFML/Config.h>
#include <SFML/System.h>
#include <SFML/Window.h>
#include <SFML/Graphics.h>
#include <SFML/Audio.h>
#endif

#include "../../bits/bitsutils.hpp"

class GamePad
{
public:
	GamePad();

	static void Init();
	#ifndef DRIVONIX_HEADLESS
	static bool Update(sfEvent event);
#endif
    static void SetButton(int gamepad, int button, bool pressed);
    static void Advance(unsigned masterCycles);
    static void SetSixButton(int gamepad, bool enabled);
	static void Write(int gamepad, int data, bool control);
	static byte ReadData(int gamepad);
	static byte ReadControl(int gamepad);

	enum MD_BUTTON
	{
		MD_UP,
		MD_DOWN,
		MD_LEFT,
		MD_RIGHT,
		MD_B,
		MD_C,
		MD_A,
		MD_START, MD_X, MD_Y, MD_Z, MD_MODE
	};

private:

	static GamePad& get(void);

	void ButtonPressed(int gamepad, int button);
	void ButtonReleased(int gamepad, int button);

	bool sixButton[2];
    unsigned phase[2];
    unsigned idle[2];
	word buttonState[2];
	word gamepadState[2];

	byte dataReg[2];
	byte controlReg[2];
	
};

#endif