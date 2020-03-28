#include "VpcmProperties.h"
#include <sys/errno.h>

namespace
{

bool parseOpt( char* string, char** name, char** value )
{
  char* p = string;
  if( *p++ != '-' || *p++ != '-' )
    return false;
  *name = p;
  while( *p && *p != '=' )
    ++p;
  if( *p )
  {
    *p++ = 0;
    *value = p;
  }
  else
    *value = 0;
  return true;
}

} // namespace

int
VpcmProperties::parse( int argc, char** argv )
{
  mode = Playback;
  rate = 44100;
  channels = 2;
  format = Float32;
  byteWidth = 4;
  raw = false;
  bufferFrames = 16384;
  eofOnIdle = true;
  posixPipe = false;
  overflow = Zeros;

  int latencyMs = 0;
  for( char** arg = argv + 1; arg < argv + argc; ++arg )
  {
    char* option, *strvalue;
    if( !parseOpt( *arg, &option, &strvalue ) )
    {
      name = *arg;
    }
    else if( strvalue )
    {
      int decvalue = 0;
      ::sscanf( strvalue, "%d", &decvalue );
      if( !::strcmp( option, "rate") )
        rate = decvalue;
      else if( !::strcmp( option, "channels" ) )
        channels = decvalue;
      else if( !::strcmp( option, "latency-msec" ) )
        latencyMs = decvalue;
      else if( !::strcmp( option, "buffer-frames" ) )
        bufferFrames = decvalue;
      else if( !::strcmp( option, "format" ) )
      {
        if( !::strcmp( strvalue, "s16" )
         || !::strcmp( strvalue, "s16le" )
         || !::strcmp( strvalue, "s16ne" ) )
            format = Int16;
        else if( !::strcmp( strvalue, "float32" )
         || !::strcmp( strvalue, "float32le" )
         || !::strcmp( strvalue, "float32ne" ) )
            format = Float32;
        else
          return EINVAL;
      }
      else if( !::strcmp( option, "overflow" ) )
      {
        if( !::strcmp( strvalue, "zeros" ) )
          overflow = Zeros;
        else if( !::strcmp( strvalue, "noise" ) )
          overflow = Noise;
        else if( !::strcmp( strvalue, "discard" ) )
          overflow = Discard;
        else
          return EINVAL;
      }
      else if( !::strcmp( option, "raw" ) )
        raw = decvalue;
      else if( !::strcmp( option, "eof-on-idle" ) )
        eofOnIdle = decvalue;
      else
        return EINVAL;
    }
    else
    {
      if( !::strcmp( option, "playback" ) )
        mode = Playback;
      else if( !::strcmp( option, "record" ) )
        mode = Record;
      else if( !::strcmp( option, "raw" ) )
        raw = true;
      else if( !::strcmp( option, "eof-on-idle" ) )
        eofOnIdle = true;
      else if( !::strcmp( option, "no-eof-on-idle" ) )
        eofOnIdle = false;
      else if( !::strcmp( option, "posix-pipe" ) )
        posixPipe = true;
      else
        return EINVAL;
    }
  }
  if( !name || !*name )
    return EINVAL;
  if( rate < 1 )
    return EINVAL;
  if( channels < 1 )
    return EINVAL;
  switch( format )
  {
    case Float32:
      byteWidth = 4;
      break;
    case Int16:
      byteWidth = 2;
      break;
    default:
      return EDEVERR;
  }
  if( bufferFrames < 2 )
    return EINVAL;
  latencyFrames = ( latencyMs * rate + 1 ) / 1000;
  if( latencyFrames < 0 )
    return EINVAL;
  if( posixPipe )
    eofOnIdle = true;
  return 0;
}

int
VpcmProperties::print( char* buf, int len, const char* sep ) const
{
  if( !sep )
    sep = " --";
  int pos = 0;
  const char* pMode = "?";
  switch( mode )
  {
    case Playback:
      pMode = "playback";
      break;
    case Record:
      pMode = "record";
      break;
  }
  const char* pFormat = "?";
  switch( format )
  {
    case Int16:
      pFormat = "s16le";
      break;
    case Float32:
      pFormat = "float32le";
      break;
  }
  const char* pOverflow = "?";
  switch( overflow )
  {
    case Zeros:
      pOverflow = "zeros";
      break;
    case Noise:
      pOverflow = "noise";
      break;
    case Discard:
      pOverflow = "discard";
      break;
  }
  pos += ::snprintf( buf + pos, len - pos,
    "%s%s"
    "%srate=%d"
    "%schannels=%d"
    "%sbuffer-frames=%d"
    "%slatency-msec=%d"
    "%sformat=%s"
    "%soverflow=%s"
    ,
    sep, pMode,
    sep, rate,
    sep, channels,
    sep, bufferFrames,
    sep, rate ? ( latencyFrames * 1000 ) / rate : 0,
    sep, pFormat,
    sep, pOverflow
  );
  if( raw )
    pos += ::snprintf( buf + pos, len - pos, "%s%s", sep, "raw" );
  if( posixPipe )
    pos += ::snprintf( buf + pos, len - pos, "%s%s", sep, "posix-pipe" );
  else if( !eofOnIdle )
    pos += ::snprintf( buf + pos, len - pos, "%s%s", sep, "no-eof-on-idle" );
  return pos;
}



