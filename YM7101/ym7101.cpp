#include "ym7101.hpp"

YM7101::YM7101()
{
	
}

YM7101& YM7101::get()
{
	static YM7101 instance;
	return instance;
}

void YM7101::Init()
{
	get().vcounter = 0;
	get().hCounter = 0;
	get().controlPortPending = false;
	get().controlPortData = 0;
	get().refresh = false;
	get().lineInt = 0xFF;
	get().maxSprites = 0;
	get().spritePixelCount = 0;
	get().maxInt = 255;
	get().spriteMask2 = 0xFF;
	get().spriteMask = std::make_pair(0xFF, std::make_pair(0xFF, 0xFF));
	get().requestInt = false;
	get().status = 0x3600;
	BitSet(get().status, 9);

	memset(get().vram, 0x0, 0x10000);

	for(int i = 0; i < 24; ++i)
	{
		get().vdpRegister[i] = 0;
	}

	for(int i = 0; i < 0x40; ++i)
	{
		get().cram[i] = 0;
	}

	for(int i = 0; i < 0x40; ++i)
	{
		get().vsram[i] = 0;
	}

	get().hCounterFirst = true;
	get().vCounterFirst = true;
}

word YM7101::GetAddressRegister()
{
	word result = get().controlPortData >> 16;
	result &= 0x3FFF;

	word bits = controlPortData & 0x3;
	result |= bits << 14;

	return result;
}

void YM7101::IncrementAddress()
{
	word address = Get().GetAddressRegister();
	address += get().vdpRegister[0xF];

	if(TestBit(address, 15))
	{
		BitSet(get().controlPortData, 1);
	}

	if(TestBit(address, 14))
	{
		BitSet(get().controlPortData, 0);
	}

	for(int i = 0; i < 14; ++i)
	{
		if(TestBit(address, 13 - i))
		{
			BitSet(get().controlPortData, 29 - i);
		}
	}
}

byte YM7101::GetHCounter()
{
	return (byte)get().hCounter;
}

byte YM7101::GetVCounter()
{
	byte result = (byte)get().vCounter;

	if(TestBit(get().vdpRegister[0xC], 1)) //interlaced ?
	{
		if(TestBit(get().vCounter, 8))
		{
			BitSet(result, 0);
		}
	}

	return result;
}

word YM7101::GetHVCounter()
{
	return ((get().GetVCounter() << 8) |((byte)get().hCounter >> 1));
}

void YM7101::WriteControlPortBYTE(byte data)
{
	word doubleData = (data << 8) | data;
	get().WriteControlPortWORD(doubleData);
}

void YM7101::WriteControlPortWORD(word data)
{
	if(!get().controlPortPending)
	{
		byte code = (byte)((data >> 14) & 0x3);

		if(code == 2)
		{
			get().UpdateRegister(data);
		}
		else
		{
			get().controlPortData &= 0xFFFF;
			get().controlPortData |= ((dword)data << 16);
			get().controlPortPending = true;
		}
	}
	else
	{
		get().controlPortPending = false;
		get().controlPortData &= 0xFFFF0000;
		get().controlPortData |= data;

		if(TestBit(get().controlPortData, 7) && TestBit(get().vdpRegister[1], 4))
		{
			get().DMA(data);
		}
	}
}

word YM7101::ReadControlPortWORD()
{
	word result = get().status;
	get().controlPortPending = false;
	BitReset(get().status, 1);
	return res;
}

void YM7101::WriteDataPortBYTE(byte data)
{
	word doubleData = (data << 8) | data;
}

void YM7101::WriteDataPortWORD(word data)
{
	int code = get().GetControlCode();

	if(code == READ_VRAM || code == READ_CRAM || code == READ_VSRAM)
	{
		//on ignore les reads
		return;
	}

	get().controlPortPending = false;

	switch(code)
	{
		case WRITE_VRAM:
		get().WriteVram(data);
		break;

		case WRITE_CRAM:
		get().WriteCram(data);
		break;

		case WRITE_VSRAM:
		get().WriteVsram(data);
		break;
	}

	IncrementAddress();
}

word YM7101::ReadDataPortWORD()
{
	int code = get().GetControlCode();

	if(code == WRITE_VRAM || code == WRITE_REG || code == WRITE_CRAM || code == WRITE_VSRAM)
	{
		//on ignore les writes
		return 0;
	}

	get().controlPortPending = false;

	word result = 0;
	switch(code)
	{
		case READ_VRAM:
		result = get().ReadVram();
		break;

		case READ_CRAM:
		result = get().ReadCram();
		break;

		case READ_VSRAM:
		result = get().ReadVsram();
		break;
	}

	get().IncrementAddress();

	return result;
}

int YM7101::GetControlCode()
{
	byte lo = (byte)((get().controlPortData >> 30) & 0x3);
	byte hi = (byte)((get().controlPortData >> 4) & 0x3);
	return ((hi << 2) | lo);
}

void YM7101::UpdateRegister(word data)
{
	byte reg = (data >> 8) & 0x7F;
	if(reg < 24)
	{
		get().vdpRegister[reg] = data & 0xFF;
	}
}

void YM7101::DMA(word data)
{
	int mode = (get().vdpRegister[23] >> 6) & 0x3;
	switch(mode)
	{
		case 0:
		case 1:
		get().DMAFromM68KToVRAM();
		break;

		case 2:
		get().DMAVRAMFill(data);
		break;

		case 3:
		get().DMAVRAMCopy();
		break;
	}
}

void YM7101::DMAFromM68KToVRAM()
{
	/*Registers 19, 20, specify how many 16-bit words to transfer:*/
	word length = (get().vdpRegister[20] << 8) | het().vdpRegister[19];

	if(length == 0x0)
	{
		length = 0xFFFF;
	}

	/*Registers 21, 22, 23 specify the source address on the 68000 side:*/
	dword address = ((get().vdpRegister[23] & 0x7F) << 17) | (get().vdpRegister[22] << 9) | (get().vdpRegister[21] << 1);

	int code = (get().controlPortData >> 30) & 0x3;

	if(TestBit(get().controlPortData, 4))
	{
		BitSet(code, 2);
	}

	for( ; length > 0; length--)
	{
		if(address > 0xFFFFFF)
		{
			address = (address - 0xFFFFFF) + 0xFF0000;
		}

		word data = Genesis::M68KReadMemoryWORD(address);

		address += 2;

		switch(code)
		{
			case WRITE_VRAM:
			get().WriteVram(data);
			break;

			case WRITE_CRAM:
			get().WriteCram(data);
			break;

			case WRITE_VSRAM:
			get().WriteVsram(data);
			break;
		}

		get().IncrementAddress();
	}

	get().vdpRegister[19] = length & 0xFF;
	get().vdpRegister[20] = (length >> 8) & 0xFF;
	get().vdpRegister[21] = (address >> 1) & 0xFF;
	get().vdpRegister[22] = (address >> 9) & 0xFF;
	get().vdpRegister[23] = (get().vdpRegister & 0x80) | ((address >> 17) & 0x7F);
}

void YM7101::DMAVRAMFill(word data)
{
	/*Registers 19, 20, specify how many 8-bit bytes to fill:*/
	word length = (get().vdpRegister[20] << 8) | get().vdpRegister[19];

	get().vram[get().GetAddressRegister()] = (byte)((data >> 8) & 0xFF);

	get().IncrementAddress();

	for( ; length > 0; length--)
	{
		get().vram[get().GetAddressRegister()] = (byte)((data >> 8) & 0xFF);

		get().IncrementAddress();
	} 
}

void YM7101::DMAVRAMCopy()
{
	/*Registers 19, 20, specify how many 8-bit bytes to copy:*/
	word length = (get().vdpRegister[20] << 8) | get().vdpRegister[19];
	/*The address bits in register 23 are ignored.
 	Registers 21, 22 specify the source address in VRAM:*/
	word address = (get().vdpRegister[22] << 8) | get().vdpRegister[21];

	for( ; length > 0; length--)
	{
		byte data = get().vram[address];
		get().vram[get().GetAddressRegister()] = data;
		get().IncrementAddress();
		address++;
	}
}

void YM7101::WriteVram(word data)
{
	word address = GetAddressRegister();

	if(TestBit(address, 0))
	{
		InvertWordEndian(data);
	}

	get().vram[address] = data >> 8;
	get().vram[address + 1] = data & 0xFF;
}

void YM7101::WriteCram(word data)
{
	word address = get().GetAddressRegister() % 0x80;
	address >>= 1;
	get().cram[address] = data;
}

void YM7101::WriteVsram(word data)
{
	word address = get().GetAddressRegister() % 0x80;
	address >>= 1;
	get().vsram[address] = data;
}

word YM7101::ReadVram()
{
	word address = GetAddressRegister();
	return get().vram[address] << 8 | get().vram[address + 1];
}

word YM7101::ReadCram()
{
	word address get().GetAddressRegister() % 0x80;
	address >>= 1;
	return get().cram[address];
}

word YM7101::ReadVsram()
{
	word address get().GetAddressRegister() % 0x80;
	address >>= 1;
	return get().vsram[address];
}