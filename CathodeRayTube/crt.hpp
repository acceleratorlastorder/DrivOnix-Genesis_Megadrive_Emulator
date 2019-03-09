#ifndef CRT_HPP
#define CRT_HPP

#include <algorithm>
#include <string>

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
	static void Render();
	static void ResetScreen();
	static void Write(int x, int y, byte red, byte green, byte blue);
	static void ApplyIntensity(int x, int y, int intensity);
	static sfRenderWindow* GetWindow();

private:
	static CRT& get(void);

	void GetConfig();

	sfRenderWindow* window;
	sfVideoMode mode;

	sfImage* screenImg;
	sfTexture* screenTex;
	sfSprite* screenSpr;

	unsigned int pixelSize;

	float sprPosX;
	float sprPosY;

	byte screenData[320][224][3];
};

#endif