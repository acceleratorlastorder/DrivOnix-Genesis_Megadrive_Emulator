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
	get().vdpResolution = std::pair<int, int>(320, 224);
	get().vCounter = 0;
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

	std::memset(get().vram, 0x0, 0x10000);

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
	word address = get().GetAddressRegister();
	address += get().vdpRegister[0xF];

	if(TestBit(address, 15))
	{
		BitSet(get().controlPortData, 1);
	}
	else
	{
		BitReset(get().controlPortData, 1);
	}

	if(TestBit(address, 14))
	{
		BitSet(get().controlPortData, 0);
	}
	else
	{
		BitReset(get().controlPortData, 0);
	}

	for(int i = 0; i < 14; ++i)
	{
		if(TestBit(address, 13 - i))
		{
			BitSet(get().controlPortData, 29 - i);
		}
		else
		{
			BitReset(get().controlPortData, 29 - i);
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
		else
		{
			BitReset(result, 0);
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
	return result;
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

	get().IncrementAddress();
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
	word length = (get().vdpRegister[20] << 8) | get().vdpRegister[19];

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
	else
	{
		BitReset(code, 2);
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
	get().vdpRegister[23] = (get().vdpRegister[23] & 0x80) | ((address >> 17) & 0x7F);
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
	word address = get().GetAddressRegister() % 0x80;
	address >>= 1;
	return get().cram[address];
}

word YM7101::ReadVsram()
{
	word address = get().GetAddressRegister() % 0x80;
	address >>= 1;
	return get().vsram[address];
}

bool YM7101::GetRequestInt()
{
	return get().requestInt;
}

int YM7101::GetIntType()
{
	int result = -1;
	if(get().requestInt)
	{
		if(TestBit(get().status, 7) && TestBit(get().vdpRegister[1], 5))
		{
			BitReset(get().status, 7);
			result = 6;
		}
		else
		{
			result = 4;
		}
	}
	get().requestInt = false;
	return result;
}

word YM7101::GetBaseLayerA()
{
	return ((get().vdpRegister[2] & 0x38) << 10);
}

word YM7101::GetBaseLayerB()
{
	return ((get().vdpRegister[3] & 0x38) << 10);
}

word YM7101::GetBaseWindow()
{
	return ((get().vdpRegister[4] & 0x38) << 13);
}

word YM7101::GetSpriteBase()
{
	word result = ((get().vdpRegister[5] & 0x7F) << 9);

	if(get().Is40Cell())
	{
		BitReset(result, 9);
	}

	return result;
}

bool YM7101::Is40Cell()
{
	if(TestBit(get().vdpRegister[0xC], 7))
	{
		return true;
	}

	if(TestBit(get().vdpRegister[0xC], 0))
	{
		return true;
	}

	return false;
}

byte YM7101::GetColorShade(byte data)
{
	if(!TestBit(get().vdpRegister[0], 2))
	{
		data &= 0x3; //8color mode
	}

	BitReset(data, 0);

	return (data << 4);
}

int YM7101::GetPatternResolution(int data)
{
	switch(data)
	{
		case 0:
		return 32;
		break;

		case 1:
		return 64;
		break;

		case 3:
		return 128;
		break;
	}

	return 32;
}

std::pair<int, int> YM7101::GetLayerSize()
{
	byte data = get().vdpRegister[0x10];
	int width = data & 0x3;
	int height = (data >> 4) & 0x3;
	return std::make_pair<int, int>(get().GetPatternResolution(width), GetPatternResolution(height));
}

word YM7101::GetVScroll(int layer, int doubleColumnNum)
{
	switch(layer)
	{
		case LAYER_A:
		{
			if(!TestBit(get().vdpRegister[0xB], 2))
			{
				return get().vsram[0x0];
			}
			else
			{
				return get().vsram[doubleColumnNum * 2];
			}
		}
		break;

		case LAYER_B:
		{
			if(!TestBit(get().vdpRegister[0xB], 2))
			{
				return get().vsram[0x1];
			}
			else
			{
				return get().vsram[(doubleColumnNum * 2) + 1];
			}
		}
		break;
	}

	return 0;
}

void YM7101::GetHScroll(int line, word& aScroll, word& bScroll)
{
	word address = ((get().vdpRegister[0xD] & 0x3F) << 10);

	byte size = get().vdpRegister[0xB] & 0x3;

	switch(size)
	{
		case 0:
		{
			aScroll = (get().vram[address] << 8) | get().vram[address + 1];
			bScroll = (get().vram[address + 2] << 8) | get().vram[address + 3];
		}
		break;

		case 1:
		{
			int value = ((line & 7) * 4);
			aScroll = (get().vram[address + value] << 8) | get().vram[address + value + 1];
			bScroll = (get().vram[address + value + 2] << 8) | get().vram[address + value + 3];
		}
		break;

		case 2:
		{
			int value = ((line & ~7) * 4);
			aScroll = (get().vram[address + value] << 8) | get().vram[address + value + 1];
			bScroll = (get().vram[address + value + 2] << 8) | get().vram[address + value + 3];
		}
		break;

		case 3:
		{
			int value = (line * 4);
			aScroll = (get().vram[address + value] << 8) | get().vram[address + value + 1];
			bScroll = (get().vram[address + value + 2] << 8) | get().vram[address + value + 3];
		}
		break;
	}
	aScroll &= 0x3FF;
	bScroll &= 0x3FF;
}

bool YM7101::SetIntensity(bool priority, int xPos, bool isSprite, int color, int palette)
{
	bool result = false;
	int newIntensity = INTENSITY_NOTSET;
	int currentIntensity = get().lineIntensity[xPos];

	if(currentIntensity == INTENSITY_NOTSET)
	{
		if(!priority)
		{
			newIntensity = INTENSITY_SHADOW;
		}
		else
		{
			newIntensity = INTENSITY_NORMAL;
		}
	}
	else if(currentIntensity == INTENSITY_SHADOW)
	{
		if(priority)
		{
			newIntensity = INTENSITY_NORMAL;
		}
	}

	if(isSprite && (palette == 3))
	{
		if(color == 14 && (currentIntensity < INTENSITY_LIGHT))
		{
			result = true;
			newIntensity = currentIntensity + 1;
		}
		else if(color == 15 && (currentIntensity > INTENSITY_SHADOW))
		{
			result = true;
			newIntensity = currentIntensity - 1;
		}
	}

	if(newIntensity != INTENSITY_NOTSET)
	{
		get().lineIntensity[xPos] = newIntensity;
	}

	return result;
}

void YM7101::RenderBackdrop()
{
	if(get().vCounter >= get().vdpResolution.second)
	{
		return;
	}
	int cramAddress = get().vdpRegister[0x7] & 0x3F;
	word color = get().cram[cramAddress];

	byte red = get().GetColorShade(color & 0xF);
	byte green = get().GetColorShade((color >> 4) & 0xF);
	byte blue = get().GetColorShade((color >> 8) & 0xF);

	for(int x = 0; x <get().vdpResolution.second; ++x)
	{
		CRT::Write(x, get().vCounter, red, green, blue);
	}
}

void YM7101::RenderLayer(word baseAddress, bool priority, int width, int height, word horizontalScroll, int layer)
{
	word hStartingCol = (horizontalScroll >> 3);
	hStartingCol = (width - (hStartingCol % width)) % width;

	word hFineScroll = (horizontalScroll & 0x7);
	hFineScroll = (8 - (hFineScroll % 8)) % 8;

	int horizontalTilesNumber = (get().vdpResolution.first / 8); //32 ou 40 selon la resolution
	int verticalTilesNumber = (get().vdpResolution.second / 8);

	if(hFineScroll != 0)
	{
		horizontalTilesNumber++;

		if(hStartingCol == 0)
		{
			hStartingCol = width - 1;
		}
		else
		{
			hStartingCol--;
		}
	}

	int tileRow = (get().vCounter / 8);
	int pixelCount = 0;

	//on draw les pixels de la scanline courante des 32/40 tiles
	for(int tilecolumn = 0; tilecolumn < horizontalTilesNumber; ++tilecolumn)
	{
		//on draw les 8 pixels du tile courant
		for(int xAdvance = hFineScroll; xAdvance < 8; ++xAdvance)
		{
			int absX = pixelCount;

			pixelCount++;

			//on récup la bonne row en fonction du vScroll
			word verticalScroll = get().GetVScroll(layer, absX / 16);

			if(TestBit(get().vdpRegister[0xC], 1))
			{
				verticalScroll >>= 1;
			}

			word vStartingRow = verticalScroll >> 3;
			word vFineScroll = verticalScroll & 0x7;

			int scrollRow = tileRow + vStartingRow;

			if(((get().vCounter % 8) + vFineScroll) > 7)
			{
				scrollRow++;
			}

			scrollRow = scrollRow % height;

			//on va récupérer la bonne address memoire qui stock les data du tile
			word nameBaseOffset = baseAddress;
			nameBaseOffset += scrollRow * (width * 2);

			int modulo = ((hStartingCol + tilecolumn) % width);
			modulo = modulo < 0 ? modulo + width : modulo; //on prend en compte le negatif
			nameBaseOffset += modulo * 2;

			//on recup la coordonnée précise de x et y du pixel
			bool hFlip = TestBit(get().vram[nameBaseOffset], 3);
			bool vFlip = TestBit(get().vram[nameBaseOffset], 4);

			int scrollYPixel = (get().vCounter + vFineScroll) % 8;

			int xPixel = xAdvance;

			if(hFlip)
			{
				xPixel = 7 - xAdvance;
			}

			xPixel = xPixel % 8;

			int yPixel = scrollYPixel;

			if(vFlip)
			{
				yPixel = 7 - scrollYPixel;
			}

			//on check la priorité
			if(TestBit(get().vram[nameBaseOffset], 7) != priority)
			{
				continue;
			}

			//on récupère les informations du tile courant
			word tileData = ((get().vram[nameBaseOffset] & 0x7) << 8) | get().vram[nameBaseOffset + 1];

			if(TestBit(get().vdpRegister[0xC], 1))
			{
				tileData *= 64;

				tileData +=  8 * yPixel;
			}
			else
			{
				tileData *= 32;

				tileData += 4 * yPixel;
			}

			//on recup les couleurs
			byte data1 = get().vram[tileData];
			byte data2 = get().vram[tileData + 1];
			byte data3 = get().vram[tileData + 2];
			byte data4 = get().vram[tileData + 3];

			int colorIndex = 0;

			switch(xPixel)
			{
				case 0 :
				colorIndex = data1 >> 4;
				break;

				case 1:
				colorIndex = data1 & 0xF;
				break;

				case 2:
				colorIndex = data2 >> 4;
				break;

				case 3:
				colorIndex = data2 & 0xF;
				break;

				case 4:
				colorIndex = data3 >> 4;
				break;

				case 5:
				colorIndex = data3 & 0xF;
				break;

				case 6:
				colorIndex = data4 >> 4;
				break;

				case 7:
				colorIndex = data4 & 0xF;
				break;
			}

			byte palette = (get().vram[nameBaseOffset] >> 5) & 0x3;

			int cramAddress = (16 * palette) + colorIndex;
			word color = get().cram[cramAddress];

			byte red = get().GetColorShade(color & 0xF);
			byte green = get().GetColorShade((color >> 4) & 0xF);
			byte blue = get().GetColorShade((color >> 8) & 0xF);

			if(TestBit(get().vdpRegister[0xC], 3))
			{
				get().SetIntensity(priority, absX, false, colorIndex, palette);
			}

			if(colorIndex == 0) //transparence
			{
				continue;
			}

			if(TestBit(get().vdpRegister[0], 5) && absX < 8) //colonne blank de gauche
			{
				continue;
			}

			CRT::Write(absX, get().vCounter, red, green, blue);
		}
		hFineScroll = 0;
	}
}

void YM7101::RenderWindow(word baseAddress, bool priority)
{
	int width = 32;
	int height = 32;

	int tileNumber = get().vdpResolution.first / 8;

	if(tileNumber == 40)
	{
		width = 64;
	}

	bool hRight = TestBit(get().vdpRegister[0x11], 7);
	byte hPos = (get().vdpRegister[0x11] & 0x1F) * 2;

	bool vDown = !TestBit(get().vdpRegister[0x12], 7);
	byte vPos = (get().vdpRegister[0x12] & 0x1F) * 8;

	if(hPos == 0 && vPos == 0)
	{
		return;
	}

	int tileRow = get().vCounter / 8;

	int startRow = vDown ? 0 : vPos;
	int endRow = vDown ? vPos : (height * 8);

	bool invertArea = (get().vCounter >= startRow) && (get().vCounter < endRow);
	
	int startCol = hRight ? hPos : 0;
	int endCol = hRight ? tileNumber : hPos;

	if(invertArea)
	{
		startCol = 0;
		endCol = tileNumber;
	}

	for(int tilecolumn = startCol; tilecolumn < endCol; ++tilecolumn)
	{
		for(int xAdvance = 0; xAdvance < 8; ++xAdvance)
		{
			word nameBaseOffset = baseAddress;
			nameBaseOffset += tileRow * (width * 2);
			nameBaseOffset += tilecolumn * 2;

			bool hFlip = TestBit(get().vram[nameBaseOffset], 3);
			bool vFlip = TestBit(get().vram[nameBaseOffset], 4);

			int xPixel = xAdvance;

			if(hFlip)
			{
				xPixel = 7 - xAdvance;
			}

			int yPixel = get().vCounter % 8;

			if(vFlip)
			{
				yPixel = 7 - (get().vCounter % 8);
			}

			int absX = (tilecolumn * 8) + xAdvance;

			if(TestBit(get().vram[nameBaseOffset], 7) != priority)
			{
				continue;
			}

			word tileData = (get().vram[nameBaseOffset] & 0x7) << 8;
			tileData |= get().vram[nameBaseOffset + 1];
			tileData *= 32;

			tileData += 4 * yPixel;

			byte data1 = get().vram[tileData];
			byte data2 = get().vram[tileData + 1];
			byte data3 = get().vram[tileData + 3];
			byte data4 = get().vram[tileData + 4];

			if(absX >= get().vdpResolution.first || (TestBit(get().vdpRegister[0], 5) && absX < 8))
			{
				continue;
			}

			int colorIndex = 0;

			switch(xPixel)
			{
				case 0 :
				colorIndex = data1 >> 4;
				break;

				case 1:
				colorIndex = data1 & 0xF;
				break;

				case 2:
				colorIndex = data2 >> 4;
				break;

				case 3:
				colorIndex = data2 & 0xF;
				break;

				case 4:
				colorIndex = data3 >> 4;
				break;

				case 5:
				colorIndex = data3 & 0xF;
				break;

				case 6:
				colorIndex = data4 >> 4;
				break;

				case 7:
				colorIndex = data4 & 0xF;
				break;
			}

			byte palette = (get().vram[nameBaseOffset] >> 5) & 0x3;

			int cramAddress = (16 * palette) + colorIndex;
			word color = get().cram[cramAddress];

			byte red = get().GetColorShade(color & 0xF);
			byte green = get().GetColorShade((color >> 4) & 0xF);
			byte blue = get().GetColorShade((color >> 8) & 0xF);

			if(TestBit(get().vdpRegister[0xC], 3))
			{
				get().SetIntensity(priority, absX, false, colorIndex, palette);
			}

			if(colorIndex == 0)
			{
				continue;
			}

			CRT::Write(absX, get().vCounter, red, green, blue);
		}
	}
}

void YM7101::RenderSprite(bool priority)
{
	int spriteCount = 0;
	word spriteBase = get().GetSpriteBase();

	bool cell40 = get().Is40Cell();
	int maxSpriteOnScanline = cell40 ? 20 : 16;
	int maxPixel = cell40 ? 320 : 256;

	int nextSprite = 0;
	int spriteIndex = -1;

	do
	{
		spriteIndex++;

		int sprite = nextSprite;
		int index = sprite * 8;

		nextSprite = get().vram[spriteBase + index + 3] & 0x7F;

		get().maxSprites--;

		if(TestBit(get().vram[spriteBase + index + 4], 7) != priority)
		{
			continue;
		}

		int yPos = get().vram[spriteBase + index] << 8;
		yPos |= get().vram[spriteBase + index + 1];

		if(TestBit(get().vdpRegister[0xC], 1))
		{
			yPos = (yPos & 0x3FE) >> 1;
		}
		else
		{
			yPos &= 0x3FF;
		}

		yPos -= 128;

		int height = (get().vram[spriteBase + index + 2] & 0x3);

		int spriteHeight = ((height + 1) * 8);

		if((get().vCounter >= yPos) && (get().vCounter < (yPos + spriteHeight)))
		{
			int hValue = (get().vram[spriteBase + index + 2] >> 2) & 0x3;

			spriteCount++;

			if(spriteCount > maxSpriteOnScanline)
			{
				BitSet(get().status, 6);
				break;
			}

			int xPos = (get().vram[spriteBase + index + 6] & 0x3) << 8;
			xPos |= get().vram[spriteBase + index + 7];

			if(xPos == 0 && !TestBit(get().vdpRegister[0xC], 1))
			{
				get().spriteMask = std::make_pair(spriteIndex, std::make_pair(yPos, yPos + spriteHeight));
			}
			else if(xPos == 1)
			{
				get().spriteMask2 = yPos;
			}

			xPos -= 128;

			byte palette = (get().vram[spriteBase + index + 4] >> 5) & 0x3;

			word tileNumber = (get().vram[spriteBase + index + 4] & 0x7) << 8;
			tileNumber |= get().vram[spriteBase + index + 5];

			bool vFlip = TestBit(get().vram[spriteBase + index + 4], 4);
			bool hFlip = TestBit(get().vram[spriteBase + index + 4], 3);

			int yIndex = get().vCounter - yPos;

			if(vFlip)
			{
				yIndex = (spriteHeight - 1) - yIndex;
			}

			int startAddress = 0;
			int memSize = 0;

			if(TestBit(get().vdpRegister[0xC], 1))
			{
				memSize = 64;
				startAddress = tileNumber * memSize;
				startAddress += (yIndex * 8);
			}
			else
			{
				memSize = 32;
				startAddress = tileNumber * memSize;
				startAddress = (yIndex * 4);
			}

			for(int width = 0; width <= hValue; ++width)
			{
				int patternIndex = width;

				if(hFlip)
				{
					patternIndex = (hValue - width);
				}

				int patternAddress = startAddress + (patternIndex * ((height + 1) * memSize));

				byte data1 = get().vram[patternIndex];
				byte data2 = get().vram[patternIndex + 1];
				byte data3 = get().vram[patternIndex + 2];
				byte data4 = get().vram[patternIndex + 3];

				for(int i = 0; i < 8; ++i)
				{
					int x = (xPos + i);

					if((x < 0) || (x >= get().vdpResolution.first) || (TestBit(get().vdpRegister[0], 5) && x < 8))
					{
						get().spritePixelCount++;
						if(get().spritePixelCount >= maxPixel)
						{
							return;
						}
						continue;
					}

					int colorIndex = 0;

					switch(hFlip ? (7 - i) : i)
					{
						case 0 :
						colorIndex = data1 >> 4;
						break;

						case 1:
						colorIndex = data1 & 0xF;
						break;

						case 2:
						colorIndex = data2 >> 4;
						break;

						case 3:
						colorIndex = data2 & 0xF;
						break;

						case 4:
						colorIndex = data3 >> 4;
						break;

						case 5:
						colorIndex = data3 & 0xF;
						break;

						case 6:
						colorIndex = data4 >> 4;
						break;

						case 7:
						colorIndex = data4 & 0xF;
						break;
					}

					int cramAddress = (16 * palette) + colorIndex;
					word color = get().cram[cramAddress];

					byte red = get().GetColorShade(color & 0xF);
					byte green = get().GetColorShade((color >> 4) & 0xF);
					byte blue = get().GetColorShade((color >> 8) & 0xF);

					bool transparentIntensity = false;
					if(TestBit(get().vdpRegister[0xC], 3) && (get().spriteLineData[x] > spriteIndex))
					{
						transparentIntensity = get().SetIntensity(priority, x, true, colorIndex, palette);
						if(transparentIntensity)
						{
						 	get().spritePixelCount++;
						 	get().spriteLineData[x] = spriteIndex;
						 	if(get().spritePixelCount >= maxPixel)
						 	{
						 		return;
						 	}
						 	continue;
						}
					}

					if(get().spriteLineData[x] != get().maxInt)
					{
						BitSet(get().status, 5);
					}

					if(get().spriteLineData[x] > spriteIndex)
					{
						get().spritePixelCount++;
						if(((red == 0xE0) && (green == 0x0) && (blue == 0xE0)) || (colorIndex == 0) || transparentIntensity)
						{
							continue;
						}

						if((get().vCounter >= get().spriteMask.second.first) && (get().vCounter <= get().spriteMask.second.second) && (spriteIndex > get().spriteMask.first) && ((get().spriteMask2 == 0xFF) || get().spriteMask.second.first == get().spriteMask2))
						{
							continue;
						}

						get().spriteLineData[x] = spriteIndex;

						CRT::Write(x, get().vCounter, red, green, blue);

						if(get().spritePixelCount >= maxPixel)
						{
							return;
						}
					}
				}
				xPos += 8;
			}
		}
	}while(nextSprite != 0 && get().maxSprites > 0);
}

void YM7101::Render()
{
	get().spritePixelCount = 0;

	if(TestBit(get().vdpRegister[0], 0))
	{
		//screen disabled
		return;
	}

	memset(&get().lineIntensity, INTENSITY_NOTSET, sizeof(int) * sizeof(get().lineIntensity));
	memset(&get().spriteLineData, get().maxInt, sizeof(byte) * sizeof(get().spriteLineData));

	get().RenderBackdrop();

	if(!TestBit(get().vdpRegister[1], 6))
	{
		//que le backdrop
		return;
	}

	word baseLayerA = get().GetBaseLayerA();
	word baseLayerB = get().GetBaseLayerB();
	word baseWindow = get().GetBaseWindow();

	std::pair<int, int> layerSize = get().GetLayerSize();

	word aHScroll = 0;
	word bHScroll = 0;

	get().GetHScroll(get().vCounter, aHScroll, bHScroll);

	bool cell40 = get().Is40Cell();

	for(int i = 0; i < 2; ++i)
	{
		get().maxSprites = cell40 ? 80 : 64;
		bool priority;
		if(i == 0)
		{
			priority = false;
		}
		else
		{
			priority = true;
		}

		get().RenderLayer(baseLayerB, priority, layerSize.first, layerSize.second, bHScroll, LAYER_B);
		get().RenderLayer(baseLayerA, priority, layerSize.first, layerSize.second, aHScroll, LAYER_A);
		get().RenderSprite(priority);
		get().RenderWindow(baseWindow, priority);
	}

	if(TestBit(get().vdpRegister[0xC], 3))
	{
		for(int i = 0; i < get().vdpResolution.first; ++i)
		{
			CRT::ApplyIntensity(i, get().vCounter, get().lineIntensity[i]);
		}
	}
}

void YM7101::Update(int clicks)
{
	bool cell40 = get().Is40Cell();

	if(get().vdpResolution.first == 256 && cell40)
	{
		get().vdpResolution = std::make_pair<int, int>(320, 224);
	}
	else if(get().vdpResolution.first == 320 && !cell40)
	{
		get().vdpResolution = std::make_pair<int, int>(256, 224);
	}

	bool nextLine = false;

	get().refresh = false;

	if((get().hCounter + clicks) > 0xFF && !get().hCounterFirst)
	{
		nextLine = true;
	}

	get().hCounter = (get().hCounter + clicks) % 0xFF;

	int jumpBackCoord = 0xE9;
	int jumpBackTo = cell40 ? 0x53 : 0x93;

	if(get().hCounterFirst && get().hCounter > jumpBackCoord)
	{
		get().hCounterFirst = false;
		int delta = get().hCounter - jumpBackCoord;
		get().hCounter = jumpBackTo + delta;
	}

	if(get().hCounter >= 0xE4 || get().hCounter < 0x8)
	{
		BitSet(get().status, 2);
	}
	else
	{
		BitReset(get().status, 2);
	}

	if(nextLine)
	{
		get().hCounterFirst = true;

		word vcount = get().vCounter;

		get().vCounter++;

		if(vcount == 0xFF)
		{
			get().vCounter = 0;
			get().vCounterFirst = true;

			BitReset(get().status, 7);
			BitReset(get().status, 5);

			CRT::ResetScreen();

			get().lineInt = get().vdpRegister[0xA];

			if(TestBit(get().vdpRegister[0xC], 1))
			{
				if(!TestBit(get().status, 4))
				{
					BitSet(get().status, 4);
				}
				else
				{
					BitReset(get().status, 4);
				}
			}

			get().spriteMask = std::make_pair(0xFF, std::make_pair(0xFF, 0xFF));
			get().spriteMask2 = 0xFF;

			get().Render();
		}
		else if((vcount == 0xEB) && get().vCounterFirst)
		{
			get().vCounterFirst = false;
			int delta = get().vCounter - 0xEB;
			get().vCounter = 0xE5 + delta;
		}
		else if(get().vCounter == get().vdpResolution.second)
		{
			BitSet(get().status, 7);
			get().refresh = true;
		}

		if(get().vCounter >= 0xE0)
		{
			BitSet(get().status, 3);
		}
		else
		{
			BitReset(get().status, 3);
		}

		if(get().vCounter >= get().vdpResolution.second)
		{
			if(get().vCounter != get().vdpResolution.second)
			{
				get().lineInt = get().vdpRegister[0xA];
			}
		}

		if(get().vCounter < get().vdpResolution.second)
		{
			get().Render();
		}

		if(get().vCounter <= get().vdpResolution.second)
		{
			bool underflow = false;

			if(get().lineInt == 0)
			{
				underflow = true;
			}

			get().lineInt--;

			if(underflow)
			{
				get().lineInt = get().vdpRegister[0xA];

				if(TestBit(get().vdpRegister[0], 0))
				{
					get().requestInt = true;
				}
			}
		}
	}

	if(TestBit(get().status, 7) && TestBit(get().vdpRegister[1], 5))
	{
		get().requestInt = true;
	}
}
