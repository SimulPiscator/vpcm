#include "Synchronization.h"
#include <sys/systm.h>

namespace Synchronization
{

// Mutex
lck_grp_attr_t* Mutex::sLockGrpAttr = 0;
lck_grp_t* Mutex::sLockGrp = 0;
int Mutex::sInstances = 0;

Mutex::Mutex()
: mMtxAttr( 0 ),
  mMtx( 0 ),
  mValid( false )
{
  if( sInstances++ == 0 )
  {
    sLockGrpAttr = ::lck_grp_attr_alloc_init();
    if( sLockGrpAttr )
    {
      ::lck_grp_attr_setstat( sLockGrpAttr );
      sLockGrp = ::lck_grp_alloc_init( __FUNCTION__, sLockGrpAttr );
    }
  }
  mMtxAttr = ::lck_attr_alloc_init();
  mMtx = ::lck_mtx_alloc_init( sLockGrp, mMtxAttr );
  mValid = true;
}

Mutex::~Mutex()
{
  mValid = false;
  ::wakeup( this );
  if( mMtx )
    ::lck_mtx_free( mMtx, sLockGrp );
  if( mMtxAttr )
    ::lck_attr_free( mMtxAttr );
  if( --sInstances == 0 )
  {
    if( sLockGrp )
      ::lck_grp_free( sLockGrp );
    if( sLockGrpAttr )
      ::lck_grp_attr_free( sLockGrpAttr );
  }
}

int
Mutex::Sleep( int timeoutMs )
{
  if( !mValid )
    return EDEVERR;
  struct timespec t = { 0, 0 };
  if( timeoutMs > 0 )
  {
    t.tv_sec = timeoutMs / 1000;
    t.tv_nsec = ( timeoutMs % 1000 ) * 1000 * 1000 + 1;
  }
  ::lck_mtx_lock( mMtx );
  return ::msleep( this, mMtx, PCATCH | PDROP, __FUNCTION__, &t );
}

void
Mutex::Wakeup()
{
  if( mValid )
    ::wakeup( this );
}

// Lock
Lock::Lock()
: mpMutex( 0 )
{
}

Lock::Lock( Mutex& m )
: mpMutex( &m )
{
  if( m.mValid )
    ::lck_mtx_lock( mpMutex->mMtx );
}

Lock::Lock( const Lock& other )
: mpMutex( other.mpMutex )
{
  other.mpMutex = 0;
}

Lock&
Lock::operator=( const Lock& other )
{
  mpMutex = other.mpMutex;
  other.mpMutex = 0;
  return *this;
}

Lock::~Lock()
{
  if( mpMutex && mpMutex->mValid )
    ::lck_mtx_unlock( mpMutex->mMtx );
}

} // namespace
