#ifndef YM7101_HPP
#define YM7101_HPP

#include <utility>
#include <array>
#include <cstdint>
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
    static void Advance(unsigned masterCycles);
    static unsigned TakeStallCycles();
    static bool Z80Interrupt();
    static unsigned Scanline();
    static unsigned LineCycles();
	static bool GetRequestInt();
	static bool GetIs256Screen();
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

	word vCounter;
	word status;
    word hvLatch;
    bool hvFrozen;

	bool is256Screen;

	bool requestInt;
	byte lineInt;

	word cram[0x40];
	word vsram[0x40];
	struct MirroredVRAM {
        byte bytes[0x10000];
        byte& operator[](unsigned address) { return bytes[address & 0xffff]; }
    } vram;
    unsigned lineCycles, scanline, stallCycles;
    bool horizontalPending, verticalPending, dmaFillPending;
    struct FifoEntry { word address, value; unsigned code; uint64_t ready; };
    std::array<FifoEntry,4> fifo;
    unsigned fifoCount;
    uint64_t totalCycles, dmaReady;
    unsigned dmaType, dmaRemaining, dmaCode;
    dword dmaSource;
    word dmaValue;
    uint64_t NextAccess(uint64_t after, unsigned slots);
    void CommitWrite(word address, unsigned code, word value);
    void DmaStep();
	byte vdpRegister[24];
	bool controlPortPending;
	dword controlPortData;

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
	void Render();

};

#endif