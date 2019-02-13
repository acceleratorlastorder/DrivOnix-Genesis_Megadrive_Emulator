#ifndef ROMLOADER_HPP
#define ROMLOADER_HPP

#include <cstring>
#include <iostream> 
#include <vector>
#include <string>
#include <fstream>

#include "../../Bits/bitsUtils.hpp"

#define SMD_HEADER_SIZE 512
#define SMD_BLOCK_SIZE 16384

/*
THE BASIC INFORMATION:
  ^^^^^^^^^^^^^^^^^^^^^

H100:    'SEGA MEGA DRIVE'                                   1
H110:    '(C)SEGA 1988.JUL'                                  2
H120:    GAME NAME (DOMESTIC)                                3
H150:    GAME NAME (OVERSEAS)                                4
H180:    'XX'                                                5
H182:    'XXXXXXX-XX'                                        6
H18E:    XXXX                                                7
H190:    'XXXXXXXXXXXXXXXX'                                  8
H1A0:    00000000, XXXXXXXX                                  9
H1A8:    RAM                                                10
H1BC:    MODEM DATA                                         11
H1C8:    MEMO                                               12
H1F0:    Country in which the product                       13
*/

class RomLoader
{
public:
	RomLoader();
	static void LoadRomFile(std::string romName);
	static void PrintRomHeader();

private:

	typedef struct
	{
		byte consoleName[0x10];
		byte copyright[0x10];
		byte domesticName[0x30];
		byte overseasName[0x30];
		byte production[0xE];
		word checksum;
		byte PCodeVNumber[0x10];
		word romStart;
		word romEnd;
		word ramStart;
		word ramEnd;
		byte sram[0xC];
		byte modem[0xC];
		byte memo[0x28];
		byte country[0x10];
	}HEADER;

	HEADER header;
	std::vector<byte> cartridgeMemory;
	std::vector<byte> sram;
	long cartridgeSize;
	bool haveSram;
	word sramStart;
	word sramEnd;
	word sramSize;

	static RomLoader& get(void);

	void LoadBINMD(std::string romName);
	void LoadSMD(std::string romName);
	void LoadHeader();
	void Checksum();
	

};

#endif