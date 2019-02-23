#ifndef GENESIS_HPP
#define GENESIS_HPP

#include <vector>
#include <windows.h>

#include <SFML/Config.h>
#include <SFML/System.h>
#include <SFML/Window.h>
#include <SFML/Graphics.h>
#include <SFML/Audio.h>

#include "RomLoader/romLoader.hpp"
#include "../M68k/m68k.hpp"
#include "../YM7101/ym7101.hpp"
#include "../CathodeRayTube/crt.hpp"

#define M68K_MEM_SIZE 0x1000000

class Genesis
{
public:
	Genesis();
	static void Init();
	static void InsertCartridge();
	static void Run();
	static void FileBrowser();
	static char* GetRomName();
	static byte M68KReadMemoryBYTE(dword address);
	static void M68KWriteMemoryBYTE(dword address, byte data);
	static word M68KReadMemoryWORD(dword address);
	static void M68KWriteMemoryWORD(dword address, word data);
	static dword M68KReadMemoryLONG(dword address);
	static void M68KWriteMemoryLONG(dword address, dword data);

private:
	char romName[2048];

	unsigned int FPS;

	bool powerOff;

	std::vector<byte> M68kMemory;

	static Genesis& get(void);

	void Update();
};

#endif