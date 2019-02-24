#include "crt.hpp"

CRT::CRT()
{

}

CRT& CRT::get(void)
{
	static CRT instance;
	return instance;
}

void CRT::Init()
{
	get().mode = (sfVideoMode){320, 224, 32};
    get().window = sfRenderWindow_create(get().mode, "DrivOnix - Chili Hot Dog Version", sfClose, NULL);
    get().screenImg = sfImage_createFromColor(320, 224, sfBlack);
    get().screenTex = sfTexture_createFromImage(get().screenImg, NULL);
	get().screenSpr = sfSprite_create();
	sfSprite_setTexture(get().screenSpr, get().screenTex, sfTrue);
	sfSprite_setPosition(get().screenSpr, (sfVector2f){ 0.0f, 0.0f });
}

void CRT::Render()
{
	for(int x = 0; x < 320; ++x)
    {
        for(int y = 0; y < 224; ++y)
        {
          	sfColor color;
          	color.r = get().screenData[x][y][0];
          	color.g = get().screenData[x][y][1];
          	color.b = get().screenData[x][y][2];
          	color.a = 255;

          	byte ix;
          	byte iy;
          	byte pixelSize = 1;
          	
          	for(ix = 0; ix < pixelSize; ++ix)
          	{
            	for(iy = 0; iy < pixelSize; ++iy)
            	{
              		sfImage_setPixel(get().screenImg, x * pixelSize + ix, y * pixelSize + iy, color);
            	}
          	}
        }
    }

    sfTexture_updateFromImage(get().screenTex, get().screenImg, 0, 0);
    sfSprite_setTexture(get().screenSpr, get().screenTex, sfTrue);

    sfRenderWindow_clear(get().window, sfBlack);
    sfRenderWindow_drawSprite(get().window, get().screenSpr, NULL);
    sfRenderWindow_display(get().window);
}

void CRT::ResetScreen()
{
	memset(get().screenData, 1, sizeof(get().screenData));
}

void CRT::Write(int x, int y, byte red, byte green, byte blue)
{
	get().screenData[x][y][0] = red;
	get().screenData[x][y][1] = green;
	get().screenData[x][y][2] = blue;
}

void CRT::ApplyIntensity(int x, int y, int intensity)
{
	byte red = get().screenData[x][y][0];
	byte green = get().screenData[x][y][1];
	byte blue = get().screenData[x][y][2];

	switch(intensity)
	{
		case INTENSITY_SHADOW:
		red /= 2;
		green /= 2;
		blue /= 2;
		break;

		case INTENSITY_LIGHT:
		red = std::min<int>(((255 - red) / 2) + red, 255);
		green = std::min<int>(((255 - green) / 2) + green, 255);
		blue = std::min<int>(((255 - blue) / 2) + blue, 255);
		break;
	}

	get().screenData[x][y][0] = red;
	get().screenData[x][y][1] = green;
	get().screenData[x][y][2] = blue;
}

sfRenderWindow* CRT::GetWindow()
{
	return get().window;
}

