#include "Genesis/genesis.hpp"

int main(void)
{

	Genesis::FileBrowser();
	RomLoader::LoadRomFile(Genesis::GetRomName());
	RomLoader::PrintRomHeader();

	Genesis::Init();
	M68k::Init();

	while(1);

	return 0;
}