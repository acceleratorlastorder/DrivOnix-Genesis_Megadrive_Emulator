#include "emu.hpp"

int main(void)
{

	Emu::FileBrowser();
	RomLoader::LoadRomFile(Emu::GetRomName());
	RomLoader::PrintRomHeader();

	while(1);

	return 0;
}

Emu::Emu()
{

}

Emu& Emu::get(void)
{
	static Emu instance;
	return instance;
}

void Emu::FileBrowser()
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

char* Emu::GetRomName()
{
	return get().romName;
}