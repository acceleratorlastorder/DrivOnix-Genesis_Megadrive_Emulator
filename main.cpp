#include "Genesis/genesis.hpp"
#include "CathodeRayTube/crt.hpp"

int main(void)
{

	Genesis::FileBrowser();
	RomLoader::LoadRomFile(Genesis::GetRomName());
	RomLoader::PrintRomHeader();

	CRT::Init();

	Genesis::Init();
	M68k::Init();
	YM7101::Init();

	while(1);

	return 0;
}