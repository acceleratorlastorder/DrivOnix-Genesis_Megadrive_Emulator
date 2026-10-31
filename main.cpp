#include "Genesis/genesis.hpp"
#include "CathodeRayTube/crt.hpp"
#include "Platform/platform.hpp"
#include "Platform/audioOutput.hpp"

#include <exception>

int main(int argc, char* argv[])
{
    bool audioEnabled=true, literal=false, provided=false;
    std::string selected;
    for(int i=1;i<argc;++i) {
        const std::string arg=argv[i];
        if(!literal && (arg=="--help" || arg=="-h")) {
            std::cout<<"Usage: DrivOnix [--no-audio] [--] [ROM.bin|ROM.md|ROM.gen|ROM.smd]\n"
                       "Run without a ROM path to open the file picker.\n";
            return 0;
        }
        if(!literal && arg=="--") {literal=true;continue;}
        if(!literal && arg=="--no-audio") {audioEnabled=false;continue;}
        if(provided || (!literal && !arg.empty() && arg[0]=='-')) {
            std::cerr<<"Usage: DrivOnix [--no-audio] [ROM path]\n";return 2;
        }
        selected=arg;provided=true;
    }

    try
    {
        if(provided && selected.empty()) throw std::runtime_error("ROM path is empty");
        const std::string romPath = provided ? selected : Platform::SelectRom();
        if (romPath.empty())
            return 0;
        Genesis::SetRomName(romPath);

        RomLoader::Init();
        RomLoader::LoadRomFile(Genesis::GetRomName());
        const byte hardwareRegion = RomLoader::GetHardwareRegion();
        RomLoader::LoadRomFile(Platform::DataFile("Bios/DrivOnix.bios"));
        Genesis::SetHardwareRegion(hardwareRegion);

        Platform::CheckDisplay();
        CRT::Init();
        AudioOutput::Init(audioEnabled);
        Genesis::AllocM68kMemory();
        Genesis::ResetM68kMemory();
        Genesis::InsertCartridge(RomLoader::Bytes(), Platform::DataFile("Bios/DrivOnix.bios"));
        Genesis::Init();
        GamePad::Init();
        M68k::Init();
        YM7101::Init();

        if (Genesis::Bios(hardwareRegion))
        {
            RomLoader::Init();
            RomLoader::LoadRomFile(Genesis::GetRomName());
            Genesis::SetHardwareRegion(hardwareRegion);
            RomLoader::PrintRomHeader();
            Genesis::ResetM68kMemory();
            Genesis::InsertCartridge();
            Genesis::Init();
            GamePad::Init();
            M68k::Init();
            YM7101::Init();
            Genesis::Run();
        }
        Genesis::SaveCartridge();
        AudioOutput::Shutdown();
        CRT::Shutdown();
        return 0;
    }
    catch (const std::exception& error)
    {
        try { Genesis::SaveCartridge(); }
        catch(const std::exception& saveError) { std::cerr<<"DrivOnix save: "<<saveError.what()<<'\n'; }
        AudioOutput::Shutdown();
        CRT::Shutdown();
        std::cerr << "DrivOnix: " << error.what() << '\n';
        return 1;
    }
}
