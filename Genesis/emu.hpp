#ifndef EMU_HPP
#define EMU_HPP

#include <windows.h>

#include "RomLoader/romLoader.hpp"

class Emu
{
public:
	Emu();
	static void FileBrowser();
	static char* GetRomName();

private:
	char romName[2048];

	static Emu& get(void);
};

#endif