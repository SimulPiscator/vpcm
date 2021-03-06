#include "VpcmAudioEngine.h"
#include "VpcmAudioDevice.h"
#include "FloatEmu.h"
#include <IOKit/audio/IOAudioLevelControl.h>
#include <IOKit/audio/IOAudioToggleControl.h>
#include <IOKit/audio/IOAudioDefines.h>
#include <IOKit/IOTimerEventSource.h>

#include <sys/fcntl.h>
#include <sys/uio.h>
#include <sys/systm.h>
#include <sys/kauth.h>

#define INT64_1E9  1000000000LL

#ifndef FIONWRITE
# define FIONWRITE	_IOR('f', 119, int)
#endif
#ifndef FIONSPACE
# define FIONSPACE	_IOR('f', 118, int)
#endif

#define EOF 0x00010000
#define CLOSING 0x00020000
#define TERMINATING 0x00040000

bool
VpcmAudioEngine::Buffer::init( int bufferBytes )
{
  int flags = kIOMemoryPhysicallyContiguous | kIOMemoryThreadSafe;
  pDesc = IOBufferMemoryDescriptor::withOptions( flags, bufferBytes, 4 );
  if( !pDesc )
    return false;
  begin.v = pDesc->getBytesNoCopy();
  if( !begin.v )
    return false;
  end.c = begin.c + bufferBytes;
  ::bzero( begin.c, bufferBytes );
  return true;
}

void
VpcmAudioEngine::Buffer::free()
{
  if( pDesc )
  {
    pDesc->release();
    pDesc = 0;
  }
  begin.c = 0;
  end.c = 0;
}

namespace {

IOAudioControl*
createVolumeControl( int idx, int usage, int initialValue )
{
  return IOAudioLevelControl::createVolumeControl(
    initialValue, -15, 0, -((15*6)<<16), 0,
    kIOAudioControlChannelIDAll, kIOAudioControlChannelNameAll,
    idx, usage
  );
}

IOAudioControl*
createMuteControl( int idx, int usage, int initialValue )
{
  return IOAudioToggleControl::createMuteControl(
    initialValue,
    kIOAudioControlChannelIDAll, kIOAudioControlChannelNameAll,
    idx, usage
  );
}

} // namespace

const VpcmAudioEngine::ControlDef
VpcmAudioEngine::sAudioControls[] =
{
  { &createVolumeControl, kIOAudioControlUsageInput,  0, &VpcmAudioEngine::mGain },
  { &createVolumeControl, kIOAudioControlUsageOutput, 0, &VpcmAudioEngine::mVolume },
  { &createMuteControl,   kIOAudioControlUsageInput,  0, &VpcmAudioEngine::mMuteInput },
  { &createMuteControl,   kIOAudioControlUsageOutput, 0, &VpcmAudioEngine::mMuteOutput },
};

IOReturn
VpcmAudioEngine::onControlChanged( IOAudioControl* pControl, SInt32, SInt32 newValue )
{
  this->*sAudioControls[pControl->getControlID()].pValue = newValue;
  return kIOReturnSuccess;
}

namespace
{

IOAudioStreamFormat sFormats[] =
{
  {
    0,
    kIOAudioStreamSampleFormatLinearPCM,
    kIOAudioStreamNumericRepresentationSignedInt,
    16, 16, 0, kIOAudioStreamByteOrderLittleEndian,
    TRUE, VpcmProperties::Int16
  },
  {
    0,
    kIOAudioStreamSampleFormatLinearPCM,
    kIOAudioStreamNumericRepresentationIEEE754Float,
    32, 32, 0, kIOAudioStreamByteOrderLittleEndian,
    TRUE, VpcmProperties::Float32
  },
};

} // namespace

OSDefineMetaClassAndStructors( VpcmAudioEngine, IOAudioEngine )

bool
VpcmAudioEngine::init( const VpcmProperties* pProperties )
{
  ::bzero( &mDevIOSel, sizeof(mDevIOSel) );
  mDevIOPtr.c = 0;
  mDevIOBytesAvail = -1;
  mIOState = 0;
  mWritePosition = 0;
  mNextTime.t = 0;
  mBufferDuration.t = 0;
  mpTimer = 0;
  
  mProperties = *pProperties;
  const char* p = mProperties.name;
  if( p )
  {
    size_t size = ::strlen(p) + 1;
    mProperties.name = new char[size];
    ::memcpy( mProperties.name, p, size );
  }
  return IOAudioEngine::init( 0 );
}

void
VpcmAudioEngine::free()
{
  mBuffer.free();
  delete[] mProperties.name;
  mProperties.name = 0;
  IOAudioEngine::free();
}

bool
VpcmAudioEngine::initHardware( IOService* pProvider )
{
  if( !IOAudioEngine::initHardware( pProvider ) )
    return false;
  
  int err = DevfsDeviceNode::devCreate( ENGINE_NODE_NAME );
  if( err )
    return false;

  if( mProperties.name )
    IOAudioEngine::setDescription( mProperties.name );

  if( mProperties.rate < 1 )
    return false;
  ::nanoseconds_to_absolutetime(
    ( mProperties.bufferFrames * INT64_1E9 ) / mProperties.rate,
    &mBufferDuration.t
  );
  int bufferBytes = mProperties.bufferFrames * mProperties.channels * mProperties.byteWidth;
  if( !mBuffer.init( bufferBytes ) )
    return false;

  IOTimerEventSource::Action action = OSMemberFunctionCast(
    IOTimerEventSource::Action, this,
    &VpcmAudioEngine::onBufferTimer
  );
  mpTimer = IOTimerEventSource::timerEventSource( this, action );
  if (!mpTimer)
    return false;
  if( !workLoop )
    return false;
  workLoop->addEventSource( mpTimer );
  
  IOAudioStreamFormat* pFormat = sFormats, *formatsEnd = sFormats + sizeof(sFormats)/sizeof(*sFormats);
  while( pFormat->fDriverTag != mProperties.format && pFormat < formatsEnd )
    ++pFormat;
  if( pFormat == formatsEnd )
    return false;
  IOAudioSampleRate rate = { mProperties.rate, 0 };
  IOAudioStreamDirection d[] = { kIOAudioStreamDirectionOutput, kIOAudioStreamDirectionInput };
  for( int i = 0; i < sizeof(d)/sizeof(*d); ++i )
  {
    bool create = false;
    switch( d[i] )
    {
      case kIOAudioStreamDirectionInput:
        create = ( mProperties.mode & VpcmProperties::Record );
        break;
      case kIOAudioStreamDirectionOutput:
        create = ( mProperties.mode & VpcmProperties::Playback );
        break;
    }
    if( create )
    {
      IOAudioStream* pStream = new IOAudioStream;
      if( !pStream || !pStream->initWithAudioEngine( this, d[i], 1 ) )
        return false;
      pFormat->fNumChannels = mProperties.channels;
      pStream->addAvailableFormat( pFormat, &rate, &rate );
      pStream->setFormat( pFormat );
      pStream->setSampleBuffer( mBuffer.begin.c, mBuffer.bytes() );
      addAudioStream( pStream );
      pStream->release();
    }
  }
  setSampleRate( &rate );
  setNumSampleFramesPerBuffer( mProperties.bufferFrames );
  setSampleLatency( mProperties.latencyFrames );
  setMixClipOverhead( 20 );
  
  for( int i = 0; i < sizeof(sAudioControls)/sizeof(*sAudioControls); ++i )
  {
    const ControlDef* pDef = sAudioControls + i;
    IOAudioControl* pControl = pDef->create( i, pDef->usage, pDef->initialValue );
    if( !pControl )
      return false;
    IOAudioControl::IntValueChangeHandler onChange =
      OSMemberFunctionCast
      (
        IOAudioControl::IntValueChangeHandler,
        this, &VpcmAudioEngine::onControlChanged
      );  
    pControl->setValueChangeHandler( onChange, this );
    addDefaultAudioControl( pControl );
    pControl->release();
  };
  return true;
}

bool
VpcmAudioEngine::terminate( IOOptionBits options )
{
  if( mIOState & FMASK )
  {
    const int timeout = 5000, sleep = 5; // ms
    int time = 0;
    while( time < timeout && !__sync_bool_compare_and_swap( &mIOState, CLOSING, TERMINATING ) )
    {
      mIOState |= EOF;
      ::selwakeup( &mDevIOSel );
      mDevIOWait.Wakeup();
      ::IOSleep( sleep );
      time += sleep;
    }
    if( time > timeout )
      return false;
  }
  if( devDestroy() != 0 )
    return false;
  return IOAudioEngine::terminate( options );
}

void
VpcmAudioEngine::stopEngineAtPosition( IOAudioEnginePosition* endingPosition )
{
  if( mProperties.eofOnIdle && mIOState && this->numActiveUserClients == 0 )
  {
    mIOState |= EOF;
    ::selwakeup( &mDevIOSel );
    mDevIOWait.Wakeup();
  }
  IOAudioEngine::stopEngineAtPosition( endingPosition );
}

IOReturn
VpcmAudioEngine::performAudioEngineStart()
{
  Time now;
  clock_get_uptime( &now.t );
  this->takeTimeStamp( false, &now.a );
  mNextTime.t = now.t + mBufferDuration.t;
  mpTimer->wakeAtTime( mNextTime.a );
  mDevIOBytesAvail = -1;
  mWritePosition = 0;
  return kIOReturnSuccess;
}

IOReturn
VpcmAudioEngine::performAudioEngineStop()
{
  mpTimer->cancelTimeout();
  return kIOReturnSuccess;
}

IOReturn
VpcmAudioEngine::stopAudioEngine()
{
  mDevIOBytesAvail = -1;
  return IOAudioEngine::stopAudioEngine();
}

void
VpcmAudioEngine::onBufferTimer( IOTimerEventSource* )
{
  Time now;
  clock_get_uptime( &now.t );
  while( mNextTime.t <= now.t )
    mNextTime.t += mBufferDuration.t;
  takeTimeStamp( true, &now.a );
  mpTimer->wakeAtTime( mNextTime.a );
}

UInt32
VpcmAudioEngine::getCurrentSampleFrame()
{
  return mWritePosition;
}

void
VpcmAudioEngine::resetClipPosition( IOAudioStream*, UInt32 )
{
  mDevIOBytesAvail = -1;
}

IOReturn
VpcmAudioEngine::eraseOutputSamples( const void* mixBuf, void*, UInt32 firstSampleFrame, UInt32 numSampleFrames, const IOAudioStreamFormat* streamFormat, IOAudioStream* audioStream )
{ // We want the mix buffer to be erased but not the sample buffer.
  return IOAudioEngine::eraseOutputSamples( mixBuf, 0, firstSampleFrame, numSampleFrames, streamFormat, audioStream );
}

IOReturn
VpcmAudioEngine::clipOutputSamples( const void* inpSrc, void*, UInt32 inFrameOffset, UInt32 inFrameCount, const IOAudioStreamFormat*, IOAudioStream* )
{
  int channels = mProperties.channels,
      valueOffset = channels * inFrameOffset,
      valueCount = channels * inFrameCount,
      bytesPerValue = mProperties.byteWidth;

  DataPtr src = { const_cast<void*>( inpSrc ) }, dest = mBuffer.begin;
  src.f += valueOffset;
  dest.c += valueOffset * bytesPerValue;
  if( mMuteOutput )
    ::bzero( dest.c, valueCount * bytesPerValue );
  else
  {
    if( !mProperties.raw )
    {
      FloatEmu::Scale( src.f, valueCount, mVolume );
      FloatEmu::Clip( src.f, valueCount );
    }
    switch( mProperties.format )
    {
      case VpcmProperties::Int16:
        FloatEmu::FloatToInt16Copy( dest.s, src.f, valueCount );
        break;
      case VpcmProperties::Float32:
        ::memcpy( dest.c, src.c, valueCount * bytesPerValue );
        break;
    }
  }
  if( mDevIOBytesAvail < 0 )
  {
    mDevIOPtr = dest;
    mDevIOBytesAvail = 0;
  }
  if( __sync_add_and_fetch( &mDevIOBytesAvail, valueCount * bytesPerValue ) > 0 )
    ::selwakeup( &mDevIOSel );
  mDevIOWait.Wakeup();
  mWritePosition = ( inFrameOffset + inFrameCount ) % numSampleFramesPerBuffer;
  return kIOReturnSuccess;
}

IOReturn
VpcmAudioEngine::convertInputSamples( const void*, void* inpDest, UInt32 inFrameOffset, UInt32 inFrameCount, const IOAudioStreamFormat*, IOAudioStream* )
{
  int channels = mProperties.channels,
      valueOffset = channels * inFrameOffset,
      valueCount = channels * inFrameCount,
      bytesPerValue = mProperties.byteWidth;
  
  DataPtr src = mBuffer.begin, dest = { inpDest };
  src.c += valueOffset * bytesPerValue;
  if( mMuteInput )
    ::bzero( dest.f, valueCount * sizeof(float) );
  else
  {
    int invalid = 0;
    if( mDevIOBytesAvail < 0 )
      invalid = valueCount;
    else
    {
      int valid = ( mBuffer.bytes() - mDevIOBytesAvail ) / bytesPerValue;
      invalid = max( 0, valueCount - valid );
    }
    ::bzero( dest.f, invalid * sizeof(float) );
    dest.f += invalid;
    src.c += invalid * bytesPerValue;
    valueCount -= invalid;
    switch( mProperties.format )
    {
      case VpcmProperties::Int16:
        FloatEmu::Int16ToFloatCopy( dest.f, src.s, valueCount );
        break;
      case VpcmProperties::Float32:
        ::memcpy( dest.c, src.c, valueCount * bytesPerValue );
        break;
    }
    if( !mProperties.raw )
    {
      FloatEmu::Scale( dest.f, valueCount, mGain );
      FloatEmu::Clip( dest.f, valueCount );
    }
  }
  if( mDevIOBytesAvail < 0 )
  {
    src.c += valueCount * bytesPerValue;
    if( src.c >= mBuffer.end.c )
       src = mBuffer.begin;
    mDevIOPtr = src;
    mDevIOBytesAvail = mBuffer.bytes();
  }
  if( __sync_add_and_fetch( &mDevIOBytesAvail, valueCount * bytesPerValue ) > 0 )
    ::selwakeup( &mDevIOSel );
  mDevIOWait.Wakeup();
  return kIOReturnSuccess;
}

#if TARGET_OS_OSX && TARGET_CPU_ARM64
bool VpcmAudioEngine::driverDesiresHiResSampleIntervals() {
    return false;
}
#endif

int
VpcmAudioEngine::getIOFlags() const
{
  return mIOState & FMASK;
}

int
VpcmAudioEngine::devOpen( int flags )
{
  int err = 0;
  if( mProperties.mode == VpcmProperties::Playback && (flags & FWRITE) )
    err = ENOTSUP;
  else if( mProperties.mode == VpcmProperties::Record && (flags & FREAD) )
    err = ENOTSUP;
  else if( (flags & FREAD) && (flags & FWRITE) )
    err = ENOTSUP;
  else if( !__sync_bool_compare_and_swap( &mIOState, 0, flags ) )
    err = EACCES;
  else
    err = workLoop->runAction(
      OSMemberFunctionCast( IOWorkLoop::Action, this, &VpcmAudioEngine::onDevOpen ),
      this
    );
  return err;
}

int
VpcmAudioEngine::onDevOpen()
{
  mDevIOBytesAvail = -1;
  ::memset( mBuffer.begin.c, 0, mBuffer.bytes() );
  return 0;
}

int
VpcmAudioEngine::devClose()
{
  mIOState = CLOSING;
  ::selthreadclear( &mDevIOSel );
  return workLoop->runAction(
    OSMemberFunctionCast( IOWorkLoop::Action, this, &VpcmAudioEngine::onDevClose ),
    this
  );
}

int
VpcmAudioEngine::onDevClose()
{
  mIOState &= ~CLOSING;
  return 0;
}

int
VpcmAudioEngine::devIoctl( u_long cmd, caddr_t data )
{
  int err = 0, arg = *(int*)data;
  int64_t result = 0;
  switch( cmd )
  {
    case FIONBIO:
      if( arg )
        __sync_or_and_fetch( &mIOState, FNONBLOCK );
      else
        __sync_and_and_fetch( &mIOState, ~FNONBLOCK );
      result = arg;
      break;
    case FIONREAD:
      result = mDevIOBytesAvail < 0 ? 0 : min( mDevIOBytesAvail, mBuffer.bytes() );
      break;
    case FIONWRITE:
      result = mDevIOBytesAvail < 0 ? 0 : mBuffer.bytes() - min( mDevIOBytesAvail, mBuffer.bytes() );
      break;
    case FIONSPACE:
      result = mDevIOBytesAvail < 0 ? 0 : min( mDevIOBytesAvail, mBuffer.bytes() );
      break;
    default:
      err = ENOTTY;
  }
  if( !err )
    *(int*)data = (int)result;
  return err;
}

int
VpcmAudioEngine::devSelect( int, void* wql, struct proc* p )
{
  if( mDevIOBytesAvail > 0 )
    return 1;
  ::selrecord( p, &mDevIOSel, wql );
  return 0;
}

int
VpcmAudioEngine::devRead( struct uio* uio )
{
  return devReadWrite( uio );
}

int
VpcmAudioEngine::devWrite( struct uio* uio )
{
  return devReadWrite( uio );
}

int
VpcmAudioEngine::devReadWrite( struct uio* uio )
{
  user_ssize_t resid = ::uio_resid( uio );
  int rw = ::uio_rw( uio );
  if( resid < 1 )
    return 0;
  
  while( mDevIOBytesAvail < 1 )
  {
    if( mProperties.posixPipe && numActiveUserClients < 1 )
      return EPIPE;
    if( mIOState & EOF )
      return rw == UIO_READ ? 0 : EPIPE;
    if( mIOState & FNONBLOCK )
      return EWOULDBLOCK;
    if( mIOState & TERMINATING )
      return EDEVERR;
    int err = mDevIOWait.Sleep();
    if( err )
      return err;
  }

  if( mDevIOBytesAvail > mBuffer.bytes() && mProperties.overflow == VpcmProperties::Discard )
    mDevIOBytesAvail = mBuffer.bytes();

  int avail = mDevIOBytesAvail,
      transferred = 0,
      err = 0;
  if( avail > mBuffer.bytes() )
  {
    uint32_t fill[256] = { 0 };
    while( avail > mBuffer.bytes() && resid > 0 && !err )
    {
      if( rw == UIO_READ && mProperties.overflow == VpcmProperties::Noise )
        for( size_t i = 0; i < sizeof(fill)/sizeof(*fill); ++i )
          fill[i] = ::random();
      int64_t bytes = min( avail - mBuffer.bytes(), sizeof(fill) );
      err = ::uiomove( reinterpret_cast<char*>( fill ), (int)bytes, uio );
      user_ssize_t newResid = ::uio_resid( uio );
      int bytesTransferred = int( resid - newResid );
      avail -= bytesTransferred;
      transferred += bytesTransferred;
      resid = newResid;
    }
  }
  while( avail > 0 && resid > 0 && !err )
  {
    int64_t bytes = min( avail, mBuffer.end.c - mDevIOPtr.c );
    err = ::uiomove( mDevIOPtr.c, (int)bytes, uio );
    user_ssize_t newResid = ::uio_resid( uio );
    int bytesTransferred = int( resid - newResid );
    DataPtr p = { mDevIOPtr.c + bytesTransferred };
    if( p.c > mBuffer.end.c )
      return EDEVERR;
    if( p.c == mBuffer.end.c )
      p = mBuffer.begin;
    mDevIOPtr = p;
    avail -= bytesTransferred;
    transferred += bytesTransferred;
    resid = newResid;
  }
  __sync_sub_and_fetch( &mDevIOBytesAvail, transferred );
  return err;
}


