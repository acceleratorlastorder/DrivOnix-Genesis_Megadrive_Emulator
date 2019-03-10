#include "genesis.hpp"

Genesis::Genesis()
{
	this->isPAL = false;
}

void Genesis::Init()
{
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

	get().FPS = get().isPAL ? 50 : 60;
}

void Genesis::SetRegionToPAL(bool pal)
{
	get().isPAL = pal;
}

void Genesis::ResetM68kMemory()
{
	for(int i = 0; i < M68K_MEM_SIZE; ++i)
	{
		get().M68kMemory[i] = 0x00;
	}
}

void Genesis::AllocM68kMemory()
{
	get().M68kMemory.reserve(M68K_MEM_SIZE);
}

void Genesis::InsertCartridge()
{
	RomLoader::PageMemory(0, 0x400000, 0, get().M68kMemory);
}

void Genesis::Bios(byte country)
{
	sfClock* clock;
    clock = sfClock_create();

    sfEvent event;

	const double VdpUpdateInterval = 1000/get().FPS;
	get().cycleThisFrame = get().isPAL ? (M68K_PAL_CYCLES_PER_SECOND / get().FPS) : (M68K_NTSC_CYCLES_PER_SECOND / get().FPS);

    double lastFrameTime = 0;
    
    get().powerOff = 0;

    while(sfRenderWindow_isOpen(CRT::GetWindow()) && !get().powerOff) //emu loop
    {
        while(sfRenderWindow_pollEvent(CRT::GetWindow(), &event))
        {
      		if (event.type == sfEvtClosed)
      		{
          		sfRenderWindow_close(CRT::GetWindow());
      		}

      		get().powerOff = GamePad::Update(event);
    	}

    	double currentTime = sfTime_asMilliseconds(sfClock_getElapsedTime(clock));

    	if((lastFrameTime + VdpUpdateInterval) <= currentTime)
    	{
      		lastFrameTime = currentTime;
      			
      		get().Update();
      		Genesis::M68KWriteMemoryBYTE(0xFF00FE, country);
      		CRT::Render();
    	}

    	if(Genesis::M68KReadMemoryLONG(0xFF00FF) == 0xCAFE) //magic number
    	{
    		break;
    	}
  	}
}

void Genesis::Run()
{

	sfClock* clock;
    clock = sfClock_create();

    sfEvent event;

	const double VdpUpdateInterval = 1000/get().FPS;

	get().cycleThisFrame = get().isPAL ? (M68K_PAL_CYCLES_PER_SECOND / get().FPS) : (M68K_NTSC_CYCLES_PER_SECOND / get().FPS);

    double lastFrameTime = 0;
    
    get().powerOff = 0;

    while(sfRenderWindow_isOpen(CRT::GetWindow()) && !get().powerOff) //emu loop
    {
        while(sfRenderWindow_pollEvent(CRT::GetWindow(), &event))
        {
      		if (event.type == sfEvtClosed)
      		{
          		sfRenderWindow_close(CRT::GetWindow());
      		}

      		get().powerOff = GamePad::Update(event);
    	}

    	double currentTime = sfTime_asMilliseconds(sfClock_getElapsedTime(clock));

    	if((lastFrameTime + VdpUpdateInterval) <= currentTime)
    	{
      		lastFrameTime = currentTime;
      			
      		get().Update();
      	
      		CRT::Render();
    	}
  	}
}

void Genesis::Update()
{
	//INIT_TIMER
	//START_TIMER
	int cycleThisUpdate = 0;

	while(cycleThisUpdate < get().cycleThisFrame)
	{
		int CPUcycles = M68k::Update();

		//printf("CPU Cycles : %d\n", CPUcycles);

		YM7101::Update(CPUcycles);

		if(YM7101::GetRequestInt())
		{
			int type = YM7101::GetIntType();
			M68k::RequestAutoVectorInt((M68k::INT_TYPE)type);
		}

		cycleThisUpdate += CPUcycles;
	}

	//STOP_TIMER("Update");
}

Genesis& Genesis::get(void)
{
	static Genesis instance;
	return instance;
}

void Genesis::FileBrowser()
{
	OPENFILENAME ofn;
	memset(&get().romName, 0, sizeof(get().romName));
    memset(&ofn,      0, sizeof(ofn));
    ofn.lStructSize  = sizeof(ofn);
    ofn.hwndOwner    = nullptr;
    ofn.lpstrFilter  = "Genesis/Megadrive BIN rom\0*.bin\0Megadrive MD rom\0*.md\0Genesis GEN rom\0*.gen\0Genesis/Megadrive SMD rom\0*.smd\0";
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

byte Genesis::M68KReadMemoryBYTE(dword address)
{
	address = address % M68K_MEM_SIZE;

	//ROM Cartridge/Expension port
	if(address < 0xA00000)
	{
		return (byte)get().M68kMemory[address];
	}
	//Z80 Area
	else if(address >= 0xA00000 && address <= 0xA0FFFF)
	{
		return 0;
	}
	//I/O Registers
	else if(address >= 0xA10000 && address <= 0xA10FFF)
	{
		BitSet(address, 0);

		if(address == 0xA10001)
		{
			return (byte)RomLoader::GetVersion();
		}
		else if(address == 0xA10003)
		{
			//Manette 1 data
			return GamePad::ReadData(0);
		}
		else if(address == 0xA10005)
		{
			//Manette 2 data
			return GamePad::ReadData(1);
		}
		else if(address == 0xA10007)
		{
			//Expansion port data
			return 0x0;
		}
		else if(address == 0xA10009)
		{
			//Manette 1 control
			return GamePad::ReadControl(0);
		}
		else if(address == 0xA1000B)
		{
			//Manette 2 control
			return GamePad::ReadControl(1);
		}
		else if(address == 0xA1000D)
		{
			//Expansion port data
			return 0x0;
		}
		else
		{
			return (byte)get().M68kMemory[address];
		}
	}
	//Z80 Control
	else if(address >= 0xA11000 && address <= 0xA11FFF)
	{
		//z80bus
		return 0;
	}
	//VDP Ports
	else if(address >= 0xC00000 && address <= 0xDFFFFF)
	{
		if(address < 0xC00004)
		{
			word vdpword = YM7101::ReadDataPortWORD();

			if((address % 2) == 0)
			{
				return (byte)(vdpword >> 8);
			}
			else
			{
				return (byte)vdpword;
			}
		}
		else if(address < 0xC00008)
		{
			word vdpword = YM7101::ReadControlPortWORD();

			if((address % 2) == 0)
			{
				return (byte)(vdpword >> 8);
			}
			else
			{
				return (byte)vdpword;
			}
		}
		else if(address < 0xC0000F)
		{
			if((address % 2) == 0)
			{
				return YM7101::GetVCounter();
			}
			else
			{
				return YM7101::GetHCounter();
			}
		}
		else if(address == 0xC00011)
		{
			//PSG
			return 0;
		}
		else
		{
			return (byte)get().M68kMemory[address];
		}
	}
	//RAM (Mirrored every $FFFF)
	else if(address >= 0xE00000 && address <= 0xFFFFFF)
	{
		address = address % 0x100000;
		address += 0xE00000;
		return (byte)get().M68kMemory[address];
	}

	return 0;
}

void Genesis::M68KWriteMemoryBYTE(dword address, byte data)
{

	address = address % M68K_MEM_SIZE;

	//ROM Cartridge/Expension port
	if(address < 0xA00000)
	{
		//on peut pas écrire dans la rom
	}
	//Z80 Area
	else if(address >= 0xA00000 && address <= 0xA0FFFF)
	{
		//osef du z80
	}
	//I/O Registers
	else if(address >= 0xA10000 && address <= 0xA10FFF)
	{
		BitSet(address, 0); //pair et impair = pareil

		if(address == 0xA10003)
		{
			//Manette 1
			GamePad::Write(0, data, false);
		}
		else if(address == 0xA10005)
		{
			//Manette 2
			GamePad::Write(1, data, false);
		}
		else if(address == 0xA10007)
		{
			//Expansion port data
		}
		else if(address == 0xA10009)
		{
			//Manette 1 control
			GamePad::Write(0, data, true);
		}
		else if(address == 0xA1000B)
		{
			//Manette 2 control
			GamePad::Write(1, data, true);
		}
		else if(address == 0xA1000D)
		{
			//Expansion port data
		}
		else
		{
			get().M68kMemory[address] = data;
		}
	}
	//Z80 Control
	else if(address >= 0xA11000 && address <= 0xA11FFF)
	{
		//osef du z80
	}
	//VDP Ports
	else if(address >= 0xC00000 && address <= 0xDFFFFF)
	{
		if(address < 0xC00004)
		{
			YM7101::WriteDataPortBYTE(data);
		}
		else if(address < 0xC00008)
		{
			YM7101::WriteControlPortBYTE(data);
		}
		else if(address < 0xC00010)
		{
			//readonly
		}
		else if(address == 0xC00011)
		{
			//PSG
		}
		else
		{
			get().M68kMemory[address] = data;
		}
	}
	//RAM (Mirrored every $FFFF)
	else if(address >= 0xE00000 && address <= 0xFFFFFF)
	{
		address = address % 0x100000;
		address += 0xE00000;
		get().M68kMemory[address] = data;
	}
}

word Genesis::M68KReadMemoryWORD(dword address)
{

	address = address % M68K_MEM_SIZE;

	//ROM Cartridge/Expension port
	if(address < 0xA00000)
	{
		return (word)((get().M68kMemory[address] << 8) | get().M68kMemory[address + 1]);
	}
	//Z80 Area
	else if(address >= 0xA00000 && address <= 0xA0FFFF)
	{
		return 0;
	}
	//I/O Registers
	else if(address >= 0xA10000 && address <= 0xA10FFF)
	{
		BitSet(address, 0);

		if(address == 0xA10001)
		{
			return RomLoader::GetVersion();
		}
		else if(address == 0xA10003)
		{
			//Manette 1
			return GamePad::ReadData(0);
		}
		else if(address == 0xA10005)
		{
			//Manette 2
			return GamePad::ReadData(1);
		}
		else if(address == 0xA10007)
		{
			//Expansion port data
			return 0x0;
		}
		else if(address == 0xA10009)
		{
			//Manette 1 control
			return GamePad::ReadControl(0);
		}
		else if(address == 0xA1000B)
		{
			//Manette 2 control
			return GamePad::ReadControl(1);
		}
		else if(address == 0xA1000D)
		{
			//Expansion port data
			return 0x0;
		}
		else
		{
			return (word)get().M68kMemory[address];
		}
	}
	//Z80 Control
	else if(address >= 0xA11000 && address <= 0xA11FFF)
	{
		//z80bus
		return 0;
	}
	//VDP Ports
	else if(address >= 0xC00000 && address <= 0xDFFFFF)
	{
		if(address < 0xC00004)
		{
			return YM7101::ReadDataPortWORD();
		}
		else if(address < 0xC00008)
		{
			return YM7101::ReadControlPortWORD();
		}
		else if(address < 0xC0000F)
		{
			return YM7101::GetHVCounter();
		}
		else if(address == 0xC00011 || address == 0xC00013 || address == 0xC00015 || address == 0xC00017)
		{
			//PSG
			return 0;
		}
		else
		{
			return (word)((get().M68kMemory[address] << 8) | get().M68kMemory[address + 1]);
		}
	}
	//RAM (Mirrored every $FFFF)
	else if(address >= 0xE00000 && address <= 0xFFFFFF)
	{
		address = address % 0x100000;
		address += 0xE00000;
		return (word)((get().M68kMemory[address] << 8) | get().M68kMemory[address + 1]);
	}

	return 0;
}

void Genesis::M68KWriteMemoryWORD(dword address, word data)
{

	address = address % M68K_MEM_SIZE;

	//ROM Cartridge/Expension port
	if(address < 0xA00000)
	{
		//on peut pas écrire dans la rom
	}
	//Z80 Area
	else if(address >= 0xA00000 && address <= 0xA0FFFF)
	{
		//osef du z80
	}
	//I/O Registers
	else if(address >= 0xA10000 && address <= 0xA10FFF)
	{
		BitSet(address, 0);

		if(address == 0xA10003)
		{
			//Manette 1
			GamePad::Write(0, data, false);
		}
		else if(address == 0xA10005)
		{
			//Manette 2
			GamePad::Write(1, data, false);
		}
		else if(address == 0xA10007)
		{
			//Expansion port data
		}
		else if(address == 0xA10009)
		{
			//Manette 1 control
			GamePad::Write(0, data, true);
		}
		else if(address == 0xA1000B)
		{
			//Manette 2 control
			GamePad::Write(1, data, true);
		}
		else if(address == 0xA1000D)
		{
			//Expansion port data
		}
		else
		{
			get().M68kMemory[address] = (byte)data;
		}
	}
	//Z80 Control
	else if(address >= 0xA11000 && address <= 0xA11FFF)
	{
		//osef du z80
	}
	//VDP Ports
	else if(address >= 0xC00000 && address <= 0xDFFFFF)
	{
		if(address < 0xC00004)
		{
			YM7101::WriteDataPortWORD(data);
		}
		else if(address < 0xC00008)
		{
			YM7101::WriteControlPortWORD(data);
		}
		else if(address < 0xC00010)
		{
			//readonly
		}
		else if(address == 0xC00011 || address == 0xC00013 || address == 0xC00015 || address == 0xC00017)
		{
			//PSG
		}
		else
		{
			get().M68kMemory[address] = data;
		}
	}
	//RAM (Mirrored every $FFFF)
	else if(address >= 0xE00000 && address <= 0xFFFFFF)
	{
		address = address % 0x100000;
		address += 0xE00000;
		get().M68kMemory[address] = (byte)(data >> 8);
		get().M68kMemory[address + 1] = (byte)(data & 0xFF);
	}
}

dword Genesis::M68KReadMemoryLONG(dword address)
{
	return (dword)((get().M68KReadMemoryWORD(address) << 16) | get().M68KReadMemoryWORD(address + 2));
}

void Genesis::M68KWriteMemoryLONG(dword address, dword data)
{
	get().M68KWriteMemoryWORD(address, (word)(data >> 16));
	get().M68KWriteMemoryWORD(address + 2, (word)(data & 0xFFFF));
}
