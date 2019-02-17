#ifndef BITSUTILS_HPP
#define BITSUTILS_HPP

#include <stdint.h>

typedef uint8_t byte;
typedef uint16_t word;
typedef uint32_t dword;
typedef int8_t signed_byte;
typedef int16_t signed_word;
typedef int32_t signed_dword;

template <typename type> 
bool TestBit(type data, int position)
{
	type mask = 1 << position;
	return (data & mask) ? true : false;
}

template <typename type> 
void BitSet(type& data, int position)
{
	type mask = 1 << position;
	data |= mask;
}

template <typename type> 
void BitReset(type& data, int position)
{
	type mask = 1 << position;
	data &= ~mask;
}

template <typename type> 
int BitGetVal(type data, int position)
{
	type mask = 1 << position;
	return (data & mask) ? 1 : 0;
}

void InvertWordEndian(word& data);

void InvertDWordEndian(dword& data);


#endif