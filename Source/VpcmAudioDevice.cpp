#include "VpcmAudioDevice.h"
#include "VpcmAudioEngine.h"

#include <IOKit/audio/IOAudioControl.h>
#include <IOKit/audio/IOAudioLevelControl.h>
#include <IOKit/audio/IOAudioToggleControl.h>
#include <IOKit/audio/IOAudioDefines.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOCommandGate.h>

#include <sys/fcntl.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/uio.h>
#include <sys/conf.h>
#include <miscfs/devfs/devfs.h>


namespace {

const int cCommandBufferSize = 2048;

bool isws( char c )
{
  for( const char* p = " \t\n"; *p; ++p )
    if( c == *p ) return true;
  return false;
}

char**
buildArgv( char* pCmdline, int* pArgc )
{
  int argc = 0;
  bool withinArg = false,
       withinDoubleQuotes = false,
       withinSingleQuotes = false;
  char* p = pCmdline, *q = p;
  while( *p )
  {
    if( !isws( *p ) && !withinArg )
      withinArg = true;
    if( isws( *p ) && !withinDoubleQuotes && !withinSingleQuotes )
    {
      if( withinArg )
      {
        withinArg = false;
        *q++ = 0;
        ++argc;
      }
      ++p;
    }
    else switch( *p )
    {
      case '"':
        if( withinSingleQuotes )
          *q++ = *p++;
        else
        {
          withinDoubleQuotes = !withinDoubleQuotes;
          ++p;
        }
        break;
      case '\'':
        if( withinDoubleQuotes )
          *q++ = *p++;
        else
        {
          withinSingleQuotes = !withinSingleQuotes;
          ++p;
        }
        break;
      case '\\':
        if( !withinSingleQuotes && *(p+1) )
          ++p;
        /* fall through */
      default:
        *q++ = *p++;
    }
  }
  if( withinArg )
  {
    *q++ = 0;
    ++argc;
  }
  p = pCmdline;
  char** argv = new char*[argc],
       **pArg = argv;
  while( pArg < argv + argc )
  {
    *pArg++ = p;
    while( *p )
      ++p;
    ++p;
  }
  *pArgc = argc;
  return argv;
}

} // namespace

OSDefineMetaClassAndStructors( VpcmAudioDevice, IOAudioDevice )

bool
VpcmAudioDevice::init( OSDictionary *properties )
{
  if( !IOAudioDevice::init( properties ) )
    return false;
  if( DevfsDeviceNode::devCreate( CONTROL_NODE_NAME, CONTROL_NODE_PERMISSIONS, UID_ROOT, GID_STAFF ) )
    return false;
  mpOutputBuffer = 0;
  return true;
}

void
VpcmAudioDevice::free()
{
  delete[] mpOutputBuffer;
  IOAudioDevice::free();
}


bool
VpcmAudioDevice::initHardware( IOService *provider )
{
  if (!IOAudioDevice::initHardware(provider))
      return false;
  setDeviceName( DEVICE_GUI_NAME );
  setDeviceShortName( DEVICE_SHORT_GUI_NAME );
  setManufacturerName( MANUFACTURER_NAME );
  setDeviceTransportType( kIOAudioDeviceTransportTypeVirtual );
  setDeviceCanBeDefault( kIOAudioDeviceCanBeDefaultInput | kIOAudioDeviceCanBeDefaultOutput );
  return true;
}

int
VpcmAudioDevice::devOpen( int flags )
{
  Synchronization::Lock lock( mMutex );
  int result = 0;
  if( mIOFlags )
    result = EACCES;
  else if( (flags & FNONBLOCK) )
    result = ENOTSUP;
  if( result == 0 )
  {
    mIOFlags = flags;
    if( !mpOutputBuffer )
    {
      mpOutputBuffer = new char[cCommandBufferSize];
      *mpOutputBuffer = 0;
    }
    if( !( flags & FWRITE ) && !*mpOutputBuffer )
      printInfo( 0, 0 );
  }
  return result;
}

int
VpcmAudioDevice::devClose()
{
  Synchronization::Lock lock( mMutex );
  mIOFlags = 0;
  return 0;
}

int
VpcmAudioDevice::devRead( struct uio* uio )
{
  Synchronization::Lock lock( mMutex );
  int error = 0;
  int count = min( (int)uio_resid( uio ), (int)::strlen( mpOutputBuffer ) );
  if( count > 0 )
    error = ::uiomove( mpOutputBuffer, count, uio );
  if( !error )
    *mpOutputBuffer = 0;
  return error;
}

int
VpcmAudioDevice::devWrite( struct uio* uio )
{
  Synchronization::Lock lock( mMutex );
  int error = 0;
  char* buf = 0;
  int count = min( (int)uio_resid( uio ), cCommandBufferSize - 1 );
  if( count > 0 )
    buf = new char[count+1];
  if( buf )
  {
    buf[count] = 0;
    error = ::uiomove( buf, count, uio );
    if( !error )
    {
      int argc = 0;
      char** argv = buildArgv( buf, &argc );
      error = workLoop->runAction(
        OSMemberFunctionCast( IOWorkLoop::Action, this, &VpcmAudioDevice::executeCommand ),
        this, &argc, argv
      );
      delete[] argv;
    }    
    delete[] buf;
  }
  return error;
}

int
VpcmAudioDevice::executeCommand( int* pArgc, char** argv )
{
  if( *pArgc < 1 )
    return 0;
  int result = ENOTSUP;
  if( !strcmp( *argv, "info" ) )
    result = printInfo( *pArgc, argv );
  else if( !strcmp( *argv, "create" ) )
    result = createEngine( *pArgc, argv );
  else if( !strcmp( *argv, "delete" ) )
    result = deleteEngine( *pArgc, argv );
  else if( !strcmp( *argv, "name" ) )
    result = nameEngine( *pArgc, argv );
  else if( !strcmp( *argv, "describe" ) )
    result = describeEngine( *pArgc, argv );
  return result;
}

int
VpcmAudioDevice::printInfo( int, char** )
{
  char* buf = mpOutputBuffer;
  int len = cCommandBufferSize;
  int pos = 0;
  pos += snprintf( buf + pos, len - pos, "%s, built %s %s\n", DEVICE_GUI_NAME, __DATE__, __TIME__ );
  int count = audioEngines->getCount();
  if( count == 0 )
    pos += snprintf( buf + pos, len - pos, "No device pairs.\n" );
  else
    pos += snprintf( buf + pos, len - pos, "Number of device pairs: %d\n", count );
  
  int i = 0, hidden = 0;
  OSObject* pObject = 0;
  while( ( pObject = audioEngines->getObject( i++ ) ) )
  {
    VpcmAudioEngine* pEngine = OSDynamicCast( VpcmAudioEngine, pObject );
    if( pEngine )
    {
      if( pEngine->devAccess( S_IREAD ) == 0 )
        pos += printEngineStatus( pEngine, buf + pos, len - pos );
      else
        ++hidden;
    }
  }
  if( hidden > 0 )
    pos += ::snprintf( buf + pos, len - pos, "Some device pairs hidden due to insufficient permissions.\n" );
  return 0;
}

int
VpcmAudioDevice::createEngine( int argc, char** argv )
{
  if( argc < 2 )
    return EINVAL;
  VpcmProperties prop = { 0 };
  int err = prop.parse( argc, argv );
  if( err )
    return err;
  if( findEngine( prop.name ) >= 0 )
    return EEXIST;
  VpcmAudioEngine* pEngine = new VpcmAudioEngine;
  if( !pEngine )
    return ENOMEM;
  if( !pEngine->init( &prop ) )
    return EDEVERR;
  if( activateAudioEngine( pEngine ) != 0 )
    return EDEVERR;
  pEngine->release();
  return 0;
}

int
VpcmAudioDevice::deleteEngine( int argc, char** argv )
{
  if( argc < 2 )
    return EINVAL;
  int idx = findEngine( argv[1] );
  if( idx < 0 )
    return ENOENT;
  VpcmAudioEngine* pEngine = OSDynamicCast( VpcmAudioEngine, audioEngines->getObject( idx ) );
  if( !pEngine )
    return ENOENT;
  int err = pEngine->devAccess( S_IWRITE );
  if( !err )
  {
    pEngine->stopAudioEngine();
    pEngine->terminate( kIOServiceRequired );
    pEngine->detach( this );
    audioEngines->removeObject( idx );
  }
  return err;
}

int
VpcmAudioDevice::nameEngine( int argc, char** argv )
{
  if( argc < 2 )
    return EINVAL;
  int idx = findEngine( argv[1] );
  if( idx < 0 )
    return ENOENT;
  VpcmAudioEngine* pEngine = OSDynamicCast( VpcmAudioEngine, audioEngines->getObject( idx ) );
  if( !pEngine )
    return ENOENT;
  int err = pEngine->devAccess( S_IREAD );
  if( err )
    return err;
  int pos = 0;
  pos += ::snprintf( mpOutputBuffer + pos, cCommandBufferSize - pos, "/dev/%s\n", pEngine->devName() );
  return 0;
}

int
VpcmAudioDevice::describeEngine( int argc, char** argv )
{
  if( argc < 2 )
    return EINVAL;
  int idx = findEngine( argv[1] );
  if( idx < 0 )
    return ENOENT;
  VpcmAudioEngine* pEngine = OSDynamicCast( VpcmAudioEngine, audioEngines->getObject( idx ) );
  if( !pEngine )
    return ENOENT;
  int err = pEngine->devAccess( S_IREAD );
  if( err )
    return err;
  int pos = 0;
  pos += ::snprintf( mpOutputBuffer + pos, cCommandBufferSize - pos, "%s", pEngine->getProperties()->name );
  pos += pEngine->getProperties()->print( mpOutputBuffer + pos, cCommandBufferSize - pos );
  pos += ::snprintf( mpOutputBuffer + pos, cCommandBufferSize - pos, "\n" );
  return 0;
}

int
VpcmAudioDevice::findEngine( const char* inName ) const
{
  int i = -1;
  VpcmAudioEngine* pEngine = 0;
  OSObject* pObject = 0;
  while( !pEngine && ( pObject = audioEngines->getObject( ++i ) ) )
  {
    pEngine = OSDynamicCast( VpcmAudioEngine, pObject );
    if( pEngine && ::strcmp( pEngine->getProperties()->name, inName )
                && ::strcmp( pEngine->devName(), inName ) )
      pEngine = 0;
  }
  return pEngine ? i : -1;
}

int
VpcmAudioDevice::printEngineStatus( VpcmAudioEngine* pEngine, char* buf, int len )
{
  int pos = 0;

  OSObject* p = pEngine->outputStreams->getObject( 0 );
  if( !p )
    p = pEngine->inputStreams->getObject( 0 );
  IOAudioStream* pStream = OSDynamicCast( IOAudioStream, p );
  if( !pStream )
    return 0;
  
  const char* dir = "?";
  switch( pEngine->getProperties()->mode )
  {
    case VpcmProperties::Playback:
      dir = "->";
      break;
    case VpcmProperties::Record:
      dir = "<-";
      break;
  }
  pos += ::snprintf( buf + pos, len - pos, "\"%s\" %s /dev/%s\n", pEngine->getProperties()->name, dir, pEngine->devName() );
  uint32_t numClients = pStream->numClients;
  const char* state = "closed";
  if( pEngine->getIOFlags() & FREAD )
    state = "open for reading";
  else if( pEngine->getIOFlags() & FWRITE )
    state = "open for writing";
  pos += ::snprintf( buf + pos, len - pos,
    "  CoreAudio clients: %d\n"
    "  Device node state: %s\n",
    numClients,
    state
  );
  pos += ::snprintf( buf + pos, len - pos, "  Configuration:" );
  pos += pEngine->getProperties()->print( buf + pos, len - pos, "\n\t--" );
  pos += ::snprintf( buf + pos, len - pos, "\n" );
  return pos;
}


