#ifndef BITSUTILS_HPP
#define BITSUTILS_HPP

typedef unsigned char byte;
typedef unsigned short word;
typedef unsigned int dword;
typedef signed char signed_byte;
typedef signed short signed_word;
typedef signed int signed_dword;

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
	return data;
}

template <typename type> 
void BitReset(type& data, int position)
{
	type mask = 1 << position;
	data &= ~mask;
	return data;
}

template <typename type> 
void BitGetVal(type data, int position)
{
	type mask = 1 << position;
	return (data & mask) ? 1 : 0;
}

void InvertWordEndian(word& data);

void InvertDWordEndian(dword& data);


#endif