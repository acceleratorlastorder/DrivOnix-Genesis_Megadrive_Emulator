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
	get().GetConfig();

	if(get().pixelSize == 1 || get().pixelSize == 2)
	{
		get().mode = (sfVideoMode){320 * get().pixelSize, 224 * get().pixelSize, 32};
		get().window = sfRenderWindow_create(get().mode, "DrivOnix - Chili Hot Dog Version", sfClose, NULL);
		get().sprPosX = 0.0f;
		get().sprPosY = 0.0f;
	}
	else if(get().pixelSize == 3)
	{
		sfVideoMode screenParam = sfVideoMode_getDesktopMode();

		get().mode = (sfVideoMode){screenParam.width, screenParam.height, 32};
		get().window = sfRenderWindow_create(get().mode, "DrivOnix - Chili Hot Dog Version", sfFullscreen, NULL);

		int widthMDScreen = get().pixelSize * 320;
    	int heightMDScreen = get().pixelSize * 224;
    	
    	int x = (screenParam.width / 2) - (widthMDScreen / 2);
    	int y = (screenParam.height / 2) - (heightMDScreen / 2);

    	get().sprPosX = (float)x;
		get().sprPosY = (float)y;
	}
	else
	{
		get().mode = (sfVideoMode){320 * get().pixelSize, 224 * get().pixelSize, 32};
		get().window = sfRenderWindow_create(get().mode, "DrivOnix - Chili Hot Dog Version", sfClose, NULL);
		get().sprPosX = 0.0f;
		get().sprPosY = 0.0f;
	}

    get().screenImg = sfImage_createFromColor(320 * get().pixelSize, 224 * get().pixelSize, sfBlack);
    get().screenTex = sfTexture_createFromImage(get().screenImg, NULL);
	get().screenSpr = sfSprite_create();
	sfSprite_setTexture(get().screenSpr, get().screenTex, sfTrue);
	sfSprite_setPosition(get().screenSpr, (sfVector2f){ get().sprPosX, get().sprPosY });
}

void CRT::GetConfig()
{
	std::ifstream input("Config/config.txt");
	for(int i = 0; i < 1; i++)
	{
		std::string id;
		std::string value;
		std::string empty;
		std::getline(input, id, '=');
		std::getline(input, value, ';');
		std::getline(input, empty, '\n');

		if(id.compare("ScreenSize") == 0)
		{
			if(value.at(0) == '1')
			{
				get().pixelSize = 1;
			}
			else if(value.at(0) == '2')
			{
				get().pixelSize = 2;
			}
			else if(value.at(0) == '3')
			{
				get().pixelSize = 3;
			}
			else
			{
				get().pixelSize = 1;
			}
		}
	}

	input.close();
}

void CRT::Render()
{

	int offsetScreen = 0;

	if(YM7101::GetIs256Screen())
	{
		offsetScreen = (320 - 256) / 2;
		offsetScreen *= get().pixelSize;
	}

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
          	
          	for(ix = 0; ix < get().pixelSize; ++ix)
          	{
            	for(iy = 0; iy < get().pixelSize; ++iy)
            	{
              		sfImage_setPixel(get().screenImg, x * get().pixelSize + ix, y * get().pixelSize + iy, color);
            	}
          	}
        }
    }

    sfTexture_updateFromImage(get().screenTex, get().screenImg, 0, 0);
    sfSprite_setTexture(get().screenSpr, get().screenTex, sfTrue);

    sfSprite_setPosition(get().screenSpr, (sfVector2f){ ((float)offsetScreen + get().sprPosX), get().sprPosY });

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

