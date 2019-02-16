#ifndef CRT_HPP
#define CRT_HPP

#include <algorithm>

#include <SFML/Config.h>
#include <SFML/System.h>
#include <SFML/Window.h>
#include <SFML/Graphics.h>
#include <SFML/Audio.h>

#include "../Bits/bitsUtils.hpp"
#include "../YM7101/ym7101.hpp"

class CRT
{
public:
	CRT();
	static void Init();
	static void ResetScreen();
	static void Write(int x, int y, byte red, byte green, byte blue);
	static void ApplyIntensity(int x, int y, int intensity);

private:
	static CRT& get(void);

	sfRenderWindow* window;
	sfEvent event;
	sfVideoMode mode;

	sfImage* screenImg;
	sfTexture* screenTex;
	sfSprite* screenSpr;

	byte screenData[320][224][3];
};

#endif