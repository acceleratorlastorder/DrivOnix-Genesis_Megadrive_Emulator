#include "Genesis/genesis.hpp"
#include "CathodeRayTube/crt.hpp"
#include "Platform/platform.hpp"

#include <exception>

int main(int argc, char* argv[])
{
    if (argc == 2 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h"))
    {
        std::cout << "Usage: DrivOnix [ROM.bin|ROM.md|ROM.gen|ROM.smd]\n"
                     "Run without a ROM path to open the file picker.\n";
        return 0;
    }
    if (argc > 2 && !(argc == 3 && std::string(argv[1]) == "--"))
    {
        std::cerr << "Usage: DrivOnix [ROM path]\n";
        return 2;
    }

    try
    {
        const std::string romPath = argc == 1 ? Platform::SelectRom() : argv[argc - 1];
        if (romPath.empty())
            return 0;
        Genesis::SetRomName(romPath);

        RomLoader::Init();
        RomLoader::LoadRomFile(Genesis::GetRomName());
        const byte country = RomLoader::GetCountry();
        RomLoader::LoadRomFile(Platform::DataFile("Bios/DrivOnix.bios"));

        Platform::CheckDisplay();
        CRT::Init();
        Genesis::AllocM68kMemory();
        Genesis::ResetM68kMemory();
        Genesis::InsertCartridge();
        Genesis::Init();
        GamePad::Init();
        M68k::Init();
        YM7101::Init();

        if (Genesis::Bios(country))
        {
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
        }
        CRT::Shutdown();
        return 0;
    }
    catch (const std::exception& error)
    {
        CRT::Shutdown();
        std::cerr << "DrivOnix: " << error.what() << '\n';
        return 1;
    }
}
