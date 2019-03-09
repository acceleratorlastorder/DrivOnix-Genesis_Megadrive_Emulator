#include "Genesis/genesis.hpp"
#include "CathodeRayTube/crt.hpp"

int main(void)
{
	//freopen("output.txt", "w", stdout);

	Genesis::FileBrowser();

	//Bios
	RomLoader::Init();

	RomLoader::LoadRomFile(Genesis::GetRomName());
	byte country = RomLoader::GetCountry();

	RomLoader::LoadRomFile("Bios/DrivOnix.bios");

	RomLoader::PrintRomHeader();

	CRT::Init();

	Genesis::AllocM68kMemory();
	Genesis::ResetM68kMemory();

	Genesis::InsertCartridge();

	Genesis::Init();
	GamePad::Init();
	
	M68k::Init();
	YM7101::Init();

	Genesis::Bios(country);
	//Bios

	//Rom
	RomLoader::Init();
	RomLoader::LoadRomFile(Genesis::GetRomName());
	RomLoader::PrintRomHeader();

	Genesis::ResetM68kMemory();
	Genesis::InsertCartridge();

	Genesis::Init();
	GamePad::Init();
	
	M68k::Init();
	YM7101::Init();

	Genesis::Run();
	//Rom

	return 0;
}