#ifndef YM7101_HPP
#define YM7101_HPP

#define READ_VRAM 0x0
#define WRITE_VRAM 0x1
#define WRITE_REG 0x2
#define WRITE_CRAM 0x3
#define READ_VSRAM 0x4
#define WRITE_VSRAM 0x5
#define READ_CRAM 0x8

#define LAYER_A 0
#define LAYER_B 1
#define LAYER_WINDOW 2

#define INTENSITY_NOTSET 0
#define INTENSITY_SHADOW 1
#define INTENSITY_NORMAL 2
#define INTENSITY_LIGHT 3

class YM7101
{
public:
	YM7101();

private:
	static YM7101& get(void);

	bool hCounterFirst;
	bool vCounterFirst;
	word vcounter;
	word hCounter;
	word status;
	bool refresh;

	bool requestInt;
	byte lineInt;
	int maxInt;

	word cram[0x40];
	word vsram[0x40];
	byte vram[0x10000];
	byte vdpRegister[24];
	bool controlPortPending;
	dword controlPortData;
	int maxSprites;
	int spritePixelCount;
	int lineIntensity[320];
	byte spriteLineData[320];
	std::pair<int, std::pair<int, int>> spriteMask;
	int spriteMask2;
};

#endif