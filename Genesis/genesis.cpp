#include "genesis.hpp"

int main(void)
{

	Genesis::FileBrowser();
	RomLoader::LoadRomFile(Emu::GetRomName());
	RomLoader::PrintRomHeader();

	Genesis::Init();
	M68k::Init();

	return 0;
}

Genesis::Emu()
{

}

void Genesis::Init()
{
	get().M68kMemory.reserve(0x1000000);

	get().M68kMemory[0xA10009] = 0x00;
	get().M68kMemory[0xA1000B] = 0x00;
	get().M68kMemory[0xA1000D] = 0x00;
	get().M68kMemory[0xA1000F] = 0xFF;
	get().M68kMemory[0xA10011] = 0x00;
	get().M68kMemory[0xA10013] = 0x00;
	get().M68kMemory[0xA10015] = 0xFF;
	get().M68kMemory[0xA10017] = 0x00;
	get().M68kMemory[0xA10019] = 0x00;
	get().M68kMemory[0xA1001B] = 0xFB;
	get().M68kMemory[0xA1001D] = 0x00;
	get().M68kMemory[0xA1001F] = 0x00;

}

Emu& Genesis::get(void)
{
	static Emu instance;
	return instance;
}

void Genesis::FileBrowser()
{
	OPENFILENAME ofn;
	memset(&get().romName, 0, sizeof(get().romName));
    memset(&ofn,      0, sizeof(ofn));
    ofn.lStructSize  = sizeof(ofn);
    ofn.hwndOwner    = nullptr;
    ofn.lpstrFilter  = "Genesis/Megadrive BIN rom\0*.bin\0Genesis/Megadrive MD rom\0*.md\0Genesis/Megadrive SMD rom\0*.smd\0";
    ofn.lpstrFile    = get().romName;
    ofn.nMaxFile     = 2048;
    ofn.lpstrTitle   = "Select a Rom, hell yeah !";
    ofn.Flags        = OFN_DONTADDTORECENT | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    GetOpenFileNameA(&ofn);
}

char* Genesis::GetRomName()
{
	return get().romName;
}