#include "m68k.hpp"

M68k::M68k()
{
	this->programCounter = 0x200;
	this->CCR = 0;
	this->stop = false;
	this->supervisorStackPointer = 0;
	this->userStackPointer = 0;
	this->superVisorModeActivated = true;
	this->unitTests = false;
}

M68k& M68k::get()
{
	static M68k instance;
	return instance;
}

void M68k::SetUnitTestsMode()
{
	get().unitTests = true;
}

void M68k::Init()
{
	get().unitTests = false;
	get().CCR = 0x2700;
	get().stop = false;

	for(int i = 0; i < 8; ++i)
	{
		get().registerData[i] = 0x0;
		get().registerAddress[i] = 0x0;
	}

	get().registerAddress[0x7] = 0x0; //get().registerAddress[0x7] = readmem32(0x0);

	get().supervisorStackPointer = get().registerAddress[0x7];
	get().userStackPointer = get().registerAddress[0x7];
	get().superVisorModeActivated = true;
	get().programCounter = 0x0; //get().programCounter = readmem32(0x4);

	get().programCounterStart = get().programCounter; 

}

void M68k::SetCpuState(CPU_STATE_DEBUG cpuState)
{
	get().cpuStateDebug = cpuState;

	get().programCounter = get().cpuStateDebug.programCounter;
	get().CCR = get().cpuStateDebug.CCR;
	get().stop = get().cpuStateDebug.stop;

	for(int i = 0; i < 8; ++i)
	{
		get().registerData[i] = get().cpuStateDebug.registerData[i];
		get().registerAddress[i] = get().cpuStateDebug.registerAddress[i];
	}
}

CPU_STATE_DEBUG M68k::GetCpuState()
{
	return get().cpuStateDebug;
}

void M68k::ExecuteOpcode(word opcode)
{
	if((opcode & 0xC100) == 0xC100)
	{
		if(get().unitTests)
		{
			std::cout << "\tM68k :: Launch OpcodeABCD" << std::endl;
		}

		get().OpcodeABCD(opcode);
	}

	//copy state for unit test
	if(get().unitTests)
	{
		get().cpuStateDebug.programCounter = get().programCounter;
		get().cpuStateDebug.CCR = get().CCR;
		get().cpuStateDebug.stop = get().stop;

		for(int i = 0; i < 8; ++i)
		{
			get().cpuStateDebug.registerData[i] = get().registerData[i];
			get().cpuStateDebug.registerAddress[i] = get().registerAddress[i];
		}
	}
}

int M68k::Update()
{
	//check Interrupt()
	//check privilege()

	if(get().stop)
	{
		return 4;
	}

	get().opcodeClicks = 0;
	const word opcode = 0x0; //readmem16(programcounter);

	get().programCounterStart = get().programCounter;
	get().programCounter += 2;

	get().ExecuteOpcode(opcode);

	return get().opcodeClicks;
}

void M68k::OpcodeABCD(word opcode)
{
	byte rx = (opcode >> 9) & 0x7;
	byte rm = (opcode >> 3) & 0x7;
	byte ry = opcode & 0x7;


}