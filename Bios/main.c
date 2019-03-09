#include <genesis.h>

#include "resources.h"

int main( )
{

	u8 loading = 0;
	u8 region = 0;

	VDP_setPalette(PAL1, BiosBackground.palette->data);

	VDP_drawImageEx(PLAN_A, &BiosBackground, TILE_ATTR_FULL(PAL1, 0, 0, 0, 1), 0, 0, 0, CPU);

	VDP_waitVSync();

	waitTick(200);

	VDP_drawText("Emulator & Bios Programmer :", 1, 1);
	VDP_drawText("Julien MAGNIN", 6, 2);

	VDP_waitVSync();

	waitTick(200);

	VDP_drawText("Unit Test Programmer :", 1, 4);
	VDP_drawText("Okba HAMIDI", 6, 5);

	VDP_waitVSync();

	waitTick(200);

	VDP_drawText("Load Rom", 16, 20);

	VDP_waitVSync();

	while(1)
	{
		if(region != 2 && loading == 40)
		{
			VDP_drawText(".", 16 + 8, 20);
		}
		else if(region != 2 && loading == 80)
		{
			VDP_drawText("..", 16 + 8, 20);
		}
		else if(region != 2 && loading == 120)
		{
			VDP_drawText("...", 16 + 8, 20);
		}
		else if(region != 2 && loading == 160)
		{
			region++;
			loading = 0;
			VDP_drawText("   ", 16 + 8, 20);
		}

		if(region == 2)
		{
			u8 regionCode = *((u8*)0xFF00FE);
			if(regionCode == 'J' || regionCode == 'U')
			{
				VDP_drawText("Rom Country found !", 9, 20);
				VDP_drawText("Emulator Region : NTSC", 9, 22);
			}
			else if(regionCode == 'E' || regionCode == 'F')
			{
				VDP_drawText("Rom Country found !", 9, 20);
				VDP_drawText("Emulator Region : PAL", 9, 22);
			}
			else
			{
				VDP_drawText("Rom Country not found...", 9, 20);
				VDP_drawText("Emulator Region : NTSC", 9, 22);
			}

			waitTick(800);

			u32* memRef = (u32*)0xFF00FF;
			*memRef = 0xCAFE; //magic number ;)

			while(1);
		}

		VDP_waitVSync();

		if(region != 2)
		{
			loading++;
		}
	}

	return 0;
}
