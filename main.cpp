#include "Genesis/genesis.hpp"
#include "CathodeRayTube/crt.hpp"

int main(void)
{
	freopen("output.txt", "w", stdout);

	Genesis::FileBrowser();
	RomLoader::LoadRomFile(Genesis::GetRomName());
	RomLoader::PrintRomHeader();

	CRT::Init();

	Genesis::InsertCartridge();

	Genesis::Init();
	M68k::Init();
	YM7101::Init();

	Genesis::Run();

	return 0;
}