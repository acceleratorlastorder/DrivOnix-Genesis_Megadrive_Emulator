#ifndef GAMEPAD_HPP
#define GAMEPAD_HPP

#include <SFML/Config.h>
#include <SFML/System.h>
#include <SFML/Window.h>
#include <SFML/Graphics.h>
#include <SFML/Audio.h>

#include "../../Bits/bitsUtils.hpp"

class GamePad
{
public:
	GamePad();

	static void Init();
	static bool Update(sfEvent event);
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
		MD_START
	};

private:

	static GamePad& get(void);

	void ButtonPressed(int gamepad, int button);
	void ButtonReleased(int gamepad, int button);

	word buttonState[2];
	word gamepadState[2];

	byte dataReg[2];
	byte controlReg[2];
	
};

#endif