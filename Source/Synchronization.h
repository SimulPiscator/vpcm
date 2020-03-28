#ifndef SYNCHRONIZATION_H
#define SYNCHRONIZATION_H

#include <IOKit/IOLib.h>

namespace Synchronization
{

class Mutex;

class Lock
{
public:
  Lock();
  Lock( Mutex& );
  Lock( const Lock& );
  Lock& operator=( const Lock& );
  ~Lock();
private:
  mutable Mutex* mpMutex;
};


class Mutex
{
public:
  Mutex();
  ~Mutex();
  int Sleep( int timeoutMs = -1 );
  void Wakeup();
private:
  int mValid;
  lck_attr_t* mMtxAttr;
  lck_mtx_t* mMtx;
  static lck_grp_attr_t* sLockGrpAttr;
  static lck_grp_t* sLockGrp;
  static int sInstances;
  friend class Lock;
};

} // namespace

#endif // SYNCHRONIZATION_H
