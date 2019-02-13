#include "bitsUtils.hpp"

void InvertWordEndian(word& data)
{
	word hi = ((data & 0x00FF) << 8);
	word lo = ((data & 0xFF00) >> 8);

	data = hi | lo;
}

void InvertDWordEndian(dword& data)
{
	dword byte1 = data & 0xFF;
	dword byte2 = (data >> 8) & 0xFF;
	dword byte3 = (data >> 16) & 0xFF;
	dword byte4 = (data >> 24) & 0xFF;

	data = ((byte1 << 24) | (byte2 << 16)) | ((byte3 << 8) | byte4);
}