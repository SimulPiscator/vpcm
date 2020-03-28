#include "FloatEmu.h"

namespace FloatEmu
{

const unsigned int expShift = 23,
                   expMask = 0xff << expShift,
                   expBias = 127,
                   signMask = 1 << 31,
                   mantMask = ~( expMask | signMask ),
                   implicitBit = 1 << expShift;
  
template<class T> T min( T a, T b ) { return a < b ? a : b; }
template<class T> T max( T a, T b ) { return a > b ? a : b; }

// This function uses fast integer operations in order to scale unit-range floating-point values
// by factors of 2^(k/2) for negative integer k.
// This corresponds to 16 steps between -96 and 0 dB, and is sufficient for a simple volume control.
void
Scale( float* ioData, unsigned int inCount, signed char k )
{
  const int kHalf = -k >> 1, kOdd = k & 1;
  union { float* f; unsigned int* i; } p = { ioData };
  while( p.f < ioData + inCount )
  {
    int exp = ( *p.i & ~signMask ) >> expShift;
    if( exp ) // if( fabs(*p.f) > 1.1754942e-38 )
    {
      int expAdd = -kHalf;
      if( kOdd )
      {
        unsigned int mant = *p.i & mantMask;
        mant |= implicitBit;
                                             // assert( fabs(256/181-sqrt(2)) < 2e-4 );
        mant = ( mant * 181 + (1<<7) ) >> 8; // mant = round( mant / sqrt(2) );
        if( !( mant & implicitBit ) )
        {
          mant <<= 1;
          --expAdd;
        }
        mant &= mantMask;
        *p.i = ( *p.i & ~mantMask ) | mant;
      }
      if( expAdd )
      {
        exp += expAdd;
        *p.i &= ~expMask;
        *p.i |= exp << expShift;
      }
    }
    ++p.f;
  }
}

void
Clip( float* ioData, unsigned int inCount )
{
  const unsigned int signMask = 1 << 31;
  const union { float f; unsigned int i; } floatOne = { 1.0f };
  union { float* f; unsigned int* i; } p = { ioData };
  while( p.f < ioData + inCount )
  {
    if( ( *p.i & ~signMask ) > floatOne.i )    // if( fabs(*p.f) > 1 || isnan(*p.f) )
      *p.i = ( *p.i & signMask ) | floatOne.i; //   *p.f = sign(*p.f);
    ++p.f;
  }
}

void
FloatToInt16Copy( short* outData, const float* inData, unsigned int inCount )
{
  union { const float* f; const unsigned int* i; } p = { inData };
  short* q = outData;
  while( p.f < inData + inCount )
  {
    unsigned int exp = ( *p.i >> expShift ) & 0xff;
    if( exp ) // if( fabs(*p.f) > 1.1754942e-38 )
    {
      unsigned int mant = 0;
      if( exp >= expBias )
        mant = implicitBit;
      else
      {
        mant = ( *p.i & mantMask ) | implicitBit;
        mant >>= expBias - exp;
      }
      mant >>= 7;
      mant += 1; // round to nearest integer
      mant >>= 1;
      if( *p.i & signMask )
        *q = -min<int>( mant, 0x8000 );
      else
        *q = min<int>( mant, 0x7fff );
    }
    else
     *q = 0;

    ++p.f;
    ++q;
  }
}

void
Int16ToFloatCopy( float* outData, const short* inData, unsigned int inCount )
{
  union { float* f; unsigned int* i; } q = { outData };
  const short* p = inData;
  while( p < inData + inCount )
  {
    unsigned int sign = *p < 0 ? signMask : 0;
    unsigned int i = sign ? -*p : *p;
    if( i )
    {
      int exp = expBias;
      i <<= 8;
      while( !( i & implicitBit ) )
      {
        i <<= 1;
        --exp;
      }
      i &= ~implicitBit;
      i |= exp << expShift;
      i |= sign;
    }
    *q.i++ = i;
    ++p;
  }
}

} // namespace

