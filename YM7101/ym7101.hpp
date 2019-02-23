#ifndef YM7101_HPP
#define YM7101_HPP

#include <utility>
#include <cstring>

#include "../Bits/bitsUtils.hpp"
#include "../Genesis/genesis.hpp"
#include "../CathodeRayTube/crt.hpp"

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
	static void Init();
	static void Update(int clicks);
	static bool GetRequestInt();
	static int GetIntType();
	static byte GetHCounter();
	static byte GetVCounter();
	static word GetHVCounter();
	static void WriteControlPortBYTE(byte data);
	static void WriteControlPortWORD(word data);
	static word ReadControlPortWORD();
	static void WriteDataPortBYTE(byte data);
	static void WriteDataPortWORD(word data);
	static word ReadDataPortWORD();

private:
	static YM7101& get(void);

	std::pair<int, int> vdpResolution;

	bool hCounterFirst;
	bool vCounterFirst;
	word vCounter;
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

	word GetAddressRegister();
	void IncrementAddress();
	int GetControlCode();
	void UpdateRegister(word data);
	void DMA(word data);
	void DMAFromM68KToVRAM();
	void DMAVRAMFill(word data);
	void DMAVRAMCopy();
	void WriteVram(word data);
	void WriteCram(word data);
	void WriteVsram(word data);
	word ReadVram();
	word ReadCram();
	word ReadVsram();
	word GetBaseLayerA();
	word GetBaseLayerB();
	word GetBaseWindow();
	word GetSpriteBase();
	bool Is40Cell();
	byte GetColorShade(byte data);
	int GetPatternResolution(int data);
	std::pair<int, int> GetLayerSize();
	word GetVScroll(int layer, int doubleColumnNum);
	void GetHScroll(int line, word& aScroll, word& bScroll);
	bool SetIntensity(bool priority, int xPos, bool isSprite, int color, int palette);
	void RenderBackdrop();
	void RenderLayer(word baseAddress, bool priority, int width, int height, word horizontalScroll, int layer);
	void RenderWindow(word baseAddress, bool priority);
	void RenderSprite(bool priority);
	void Render();

};

#endif