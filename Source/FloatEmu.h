#ifndef FLOAT_EMU_H
#define FLOAT_EMU_H

namespace FloatEmu
{
// This function uses integer operations in order to scale unit-range floating-point values
// by factors of 2^(k/2) for negative integer k.
// This corresponds to 16 steps between -96 and 0 dB, and is sufficient for a simple volume control.
void Scale( float*, unsigned int count, signed char k );
// This function uses integer operations for clipping float values to the -1 .. 1 range.
void Clip( float*, unsigned int count );

void FloatToInt16Copy( short*, const float*, unsigned int count );
void Int16ToFloatCopy( float*, const short*, unsigned int count );

} // namespace

#endif // FLOAT_EMU_H
