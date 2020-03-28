#include "DevfsDeviceNode.h"

#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/kauth.h>
#include <sys/stat.h>
#include <miscfs/devfs/devfs.h>

extern "C" int seltrue( dev_t, int, void*, struct proc* );

struct cdevsw DevfsDeviceNode::sCdevsw = NO_CDEVICE;
int DevfsDeviceNode::sMajor = -1;
int DevfsDeviceNode::sInstanceCount = 0;
DevfsDeviceNode* DevfsDeviceNode::sInstances[] = { 0 };

void
DevfsDeviceNode::classInit()
{
  sCdevsw.d_open = open;
  sCdevsw.d_close = close;
  sCdevsw.d_read = read;
  sCdevsw.d_write = write;
  sCdevsw.d_ioctl = ioctl;
  sCdevsw.d_select = select;
  sCdevsw.d_type = D_TTY;
  sMajor = ::cdevsw_isfree( -1 );
  sMajor = ::cdevsw_add( sMajor, &sCdevsw );
}

void
DevfsDeviceNode::classFree()
{
  ::cdevsw_remove( sMajor, &sCdevsw );
  sMajor = -1;
}

DevfsDeviceNode::DevfsDeviceNode()
: mpName( 0 ),
  mDev( 0 ),
  mNode( 0 )
{
  if( sInstanceCount++ == 0 )
    classInit();
}

DevfsDeviceNode::~DevfsDeviceNode()
{
  devDestroy();
  if( --sInstanceCount == 0 )
    classFree();
}

int
DevfsDeviceNode::devCreate( const char* pNamePattern, int mode, int uid, int gid )
{
  if( !pNamePattern )
    return EINVAL;
  if( uid < 0 )
    mUid = ::kauth_getuid();
  else
    mUid = uid;
  if( gid < 0 )
    gid = ::kauth_getgid();
  else
    mGid = gid;
  mMode = mode;

  int minor = 0;
  while( sInstances[minor] && minor < sMaxInstances )
    ++minor;
  if( minor >= sMaxInstances )
    return ENOMEM;

  mDev = ::makedev( sMajor, minor );
  sInstances[minor] = this;

  size_t size = ::strlen(pNamePattern) + 4;
  mpName = new char[size];
  if( !mpName )
    return ENOMEM;

  int r = ::snprintf( mpName, size, pNamePattern, minor );
  if( r < 0 || r >= size )
    return EINVAL;
  mNode = ::devfs_make_node( mDev, DEVFS_CHAR, mUid, mGid, mMode, mpName );
  if( !mNode )
    return EDEVERR;
  
  return 0;
}

int
DevfsDeviceNode::devDestroy()
{
  if( mNode )
    ::devfs_remove( mNode );
  mNode = 0;
  int minor = ::minor(mDev);
  if( mDev != 0 && minor < sMaxInstances && sInstances[minor] == this )
    sInstances[minor] = 0;
  delete[] mpName;
  mpName = 0;
  return 0;
}

int
DevfsDeviceNode::devAccess( mode_t reqMode ) const
{
  kauth_cred_t cred = ::kauth_cred_get();
  if( ::kauth_cred_issuser( cred ) )
    return 0;
  mode_t mode = (mMode & S_IRWXO) << 6;
  if( ::groupmember( mGid, cred ) )
    mode |= (mMode & S_IRWXG) << 3;
  if( ::kauth_cred_getuid( cred ) == mUid )
    mode |= (mMode & S_IRWXU);
  if( (reqMode & mode) == reqMode )
    return 0;
  return EACCES;
}

DevfsDeviceNode*
DevfsDeviceNode::getInstance( dev_t dev )
{
  if( ::major( dev ) != sMajor )
    return 0;
  if( ::minor( dev ) < sMaxInstances )
    return sInstances[::minor( dev )];
  return 0;
}

int
DevfsDeviceNode::open( dev_t dev, int flags, int, struct proc* )
{
  DevfsDeviceNode* p = getInstance( dev );
  return p ? p->devOpen( flags ) : ENXIO;
}

int
DevfsDeviceNode::close( dev_t dev, int, int, struct proc* )
{
  DevfsDeviceNode* p = getInstance( dev );
  return p ? p->devClose() : ENXIO;
}

int
DevfsDeviceNode::read( dev_t dev, struct uio *uio, int /*ioflag*/ )
{
  DevfsDeviceNode* p = getInstance( dev );
  return p ? p->devRead( uio ) : ENXIO;
}

int
DevfsDeviceNode::write( dev_t dev, struct uio *uio, int /*ioflag*/ )
{
  DevfsDeviceNode* p = getInstance( dev );
  return p ? p->devWrite( uio ) : ENXIO;
}

int
DevfsDeviceNode::ioctl( dev_t dev, u_long cmd, caddr_t pData, int /*flag*/, struct proc* )
{
  DevfsDeviceNode* p = getInstance( dev );
  return p ? p->devIoctl( cmd, pData ) : ENXIO;
}

int
DevfsDeviceNode::select( dev_t dev, int rw, void* wql, struct proc* proc )
{
  DevfsDeviceNode* p = getInstance( dev );
  return p ? p->devSelect( rw, wql, proc ) : ENXIO;
}

int
DevfsDeviceNode::devRead( struct uio* )
{
  return ENOTSUP;
}

int
DevfsDeviceNode::devWrite( struct uio* )
{
  return ENOTSUP;
}

int
DevfsDeviceNode::devIoctl( u_long, caddr_t )
{
  return ENOTTY;
}

int
DevfsDeviceNode::devSelect( int, void*, struct proc* )
{
  return 1;
}


