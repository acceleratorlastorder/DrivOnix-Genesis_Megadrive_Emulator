#ifndef GENESIS_HPP
#define GENESIS_HPP

#include <vector>
#include <windows.h>

#include "RomLoader/romLoader.hpp"
#include "../M68k/m68k.hpp"

class Genesis
{
public:
	Genesis();
	static void FileBrowser();
	static char* GetRomName();

private:
	std::vector<byte> M68kMemory;

	char romName[2048];

	static Genesis& get(void);

	void Init()
};

#endif