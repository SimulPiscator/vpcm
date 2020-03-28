#ifndef DEVFS_DEVICE_NODE_H
#define DEVFS_DEVICE_NODE_H

#include <sys/types.h>

#ifndef UID_ROOT
# define UID_ROOT 0
#endif
#ifndef GID_STAFF
# define GID_STAFF 20
#endif

#define MAX_INSTANCES 32

class DevfsDeviceNode
{
public:
  DevfsDeviceNode();
  virtual ~DevfsDeviceNode();

  // If the name argument contains a single printf format specifier for an int, it
  // will be replaced with the device's minor number, e.g.
  // "myname%d" -> "myname5" for a device with minor number 5.
  int devCreate( const char* name, int mode = 0600, int gid = -1, int uid = -1 );
  int devDestroy();

  const char* devName() const { return mpName; }
  int devMajor() const { return ::major( mDev ); }
  int devMinor() const { return ::minor( mDev ); }
  int devAccess( mode_t mode ) const;
  
protected:
  virtual int devOpen( int flags ) = 0;
  virtual int devClose() = 0;
  virtual int devRead( struct uio* );
  virtual int devWrite( struct uio* );
  virtual int devIoctl( u_long cmd, caddr_t data );
  virtual int devSelect( int rw, void*, struct proc* );

private:
  char* mpName;
  void* mNode; dev_t mDev;
  uid_t mUid; gid_t mGid; mode_t mMode;

private:
  static void classInit();
  static void classFree();
  
  static int open( dev_t, int, int, struct proc* );
  static int close( dev_t, int, int, struct proc* );
  static int read( dev_t, struct uio*, int );
  static int write( dev_t, struct uio*, int );
  static int ioctl( dev_t, u_long, caddr_t, int, struct proc* );
  static int select( dev_t, int, void*, struct proc* );
  
  static DevfsDeviceNode* getInstance( dev_t );

private:
  static struct cdevsw sCdevsw;
  static int sMajor;
  static int sInstanceCount;
  static const int sMaxInstances = MAX_INSTANCES;
  static DevfsDeviceNode* sInstances[sMaxInstances];
};

#endif // DEVFS_DEVICE_NODE_H
