#ifndef BITSUTILS_HPP
#define BITSUTILS_HPP

#include <stdint.h>
#include <climits>
#include <type_traits>

typedef uint8_t byte;
typedef uint16_t word;
typedef uint32_t dword;
typedef int8_t signed_byte;
typedef int16_t signed_word;
typedef int32_t signed_dword;

template <typename type>
bool TestBit(type data, int position)
{
    if (position < 0 || position >= int(sizeof(type) * CHAR_BIT)) return false;
    typedef typename std::make_unsigned<type>::type Unsigned;
    return (static_cast<Unsigned>(data) & (Unsigned(1) << position)) != 0;
}

template <typename type>
void BitSet(type& data, int position)
{
    if (position < 0 || position >= int(sizeof(type) * CHAR_BIT)) return;
    typedef typename std::make_unsigned<type>::type Unsigned;
    data = static_cast<type>(static_cast<Unsigned>(data) | (Unsigned(1) << position));
}

template <typename type>
void BitReset(type& data, int position)
{
    if (position < 0 || position >= int(sizeof(type) * CHAR_BIT)) return;
    typedef typename std::make_unsigned<type>::type Unsigned;
    data = static_cast<type>(static_cast<Unsigned>(data) & ~(Unsigned(1) << position));
}

template <typename type>
int BitGetVal(type data, int position)
{
    return TestBit(data, position) ? 1 : 0;
}

template <typename type>
type ROL(type value, int count)
{
    typedef typename std::make_unsigned<type>::type Unsigned;
    const unsigned bits = sizeof(type) * CHAR_BIT;
    const unsigned shift = static_cast<unsigned>(count) % bits;
    const uint64_t data = static_cast<Unsigned>(value);
    return shift ? static_cast<type>((data << shift) | (data >> (bits - shift))) : value;
}

template <typename type>
type ROR(type value, int count)
{
    typedef typename std::make_unsigned<type>::type Unsigned;
    const unsigned bits = sizeof(type) * CHAR_BIT;
    const unsigned shift = static_cast<unsigned>(count) % bits;
    const uint64_t data = static_cast<Unsigned>(value);
    return shift ? static_cast<type>((data >> shift) | (data << (bits - shift))) : value;
}

void InvertWordEndian(word& data);

void InvertDWordEndian(dword& data);


#endif