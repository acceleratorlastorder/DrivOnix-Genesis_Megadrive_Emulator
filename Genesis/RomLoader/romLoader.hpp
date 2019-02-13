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

typedef struct
{
	unsigned char consoleName[0x10];
	unsigned char copyright[0x10];
	unsigned char domesticName[0x30];
	unsigned char overseasName[0x30];
	unsigned char production[0xE];
	word checksum;
	unsigned char PCodeVNumber[0x10];
	word romStart;
	word romEnd;
	word ramStart;
	word ramEnd;
	unsigned char sram[0xC];
	unsigned char modem[0xC];
	unsigned char memo[0x28];
	unsigned char country[0x10];
}HEADER;

class RomLoader
{
public:
	RomLoader();
	static void LoadRomFile(std::string romName);
	static void PrintRomHeader();

private:
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