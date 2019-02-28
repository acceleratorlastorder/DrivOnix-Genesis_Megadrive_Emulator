#ifndef GENESIS_HPP
#define GENESIS_HPP

#include <iostream>
#include <vector>
#include <windows.h>
#include <chrono>

#define TIMING
 
#ifdef TIMING
#define INIT_TIMER auto start = std::chrono::high_resolution_clock::now();
#define START_TIMER  start = std::chrono::high_resolution_clock::now();
#define STOP_TIMER(name)  std::cout << "RUNTIME of " << name << ": " << std::dec << \
    std::chrono::duration_cast<std::chrono::milliseconds>( \
            std::chrono::high_resolution_clock::now()-start \
    ).count() << " ms " << std::endl; 
#else
#define INIT_TIMER
#define START_TIMER
#define STOP_TIMER(name)
#endif

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
	static void AllocM68kMemory();
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
	static Genesis& get(void);

	char romName[2048];

	unsigned int FPS;

	bool powerOff;

	std::vector<byte> M68kMemory;

	void Update();
};

#endif