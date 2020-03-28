#ifndef VPCM_PROPERTIES_H
#define VPCM_PROPERTIES_H

#include <IOKit/audio/IOAudioEngine.h>

struct VpcmProperties
{
  int parse( int, char** );
  int print( char*, int, const char* = 0 ) const;

  enum
  {
    None = 0,
    Playback = 1, Record = 2,
    Int16 = 0, Float32 = 1,
    Zeros = 0, Discard = 1, Noise = 2,
  };
  char* name;
  int mode, overflow, format, byteWidth;
  bool raw, eofOnIdle, posixPipe;
  int bufferFrames, latencyFrames, channels, rate;
};


#endif // VPCM_PROPERTIES_H