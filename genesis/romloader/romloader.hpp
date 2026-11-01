#ifndef ROMLOADER_HPP
#define ROMLOADER_HPP

#include <cstring>
#include <iostream> 
#include <vector>
#include <string>
#include <fstream>

#include "../../bits/bitsutils.hpp"
#include "../genesis.hpp"

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
	static void Init();
	static void LoadRomFile(std::string romName,const std::string& member = "");
    // Restore an already decoded cartridge after the BIOS handoff without
    // re-reading or decompressing its source archive.
    static void LoadImage(std::vector<byte> bytes);
	static void PrintRomHeader();
	static byte GetCountry();
    // Header-selected J/U/E hardware region; GetCountry preserves raw input.
    static byte GetHardwareRegion();
    static bool ChecksumMatches();
    static const std::vector<byte>& Bytes();
	static byte GetVersion();
	static void PageMemory(int number, uint64_t pageSize, uint64_t start, std::vector<byte>& dest);

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
		dword romStart;
		dword romEnd;
		dword ramStart;
		dword ramEnd;
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
	dword sramSize;
	byte version;
    byte hardwareRegion = 'U';
    word computedChecksum;

	static RomLoader& get(void);

	void LoadSMD(const std::vector<byte>& bytes,const std::string& romName);
	void LoadHeader();
	void Checksum();
	

};

#endif
