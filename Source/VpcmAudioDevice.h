#ifndef VPCM_AUDIO_DEVICE_H
#define VPCM_AUDIO_DEVICE_H

#include <IOKit/audio/IOAudioDevice.h>
#include "DevfsDeviceNode.h"
#include "Synchronization.h"
#include "VpcmAudioEngine.h"

#define DEVICE_GUI_NAME "vpcm Virtual Audio Device"
#define DEVICE_SHORT_GUI_NAME "vpcm"
#define MANUFACTURER_NAME "vpcm"

#define CONTROL_NODE_NAME "vpcmctl"
#define ENGINE_NODE_NAME "vpcm%d"
#define CONTROL_NODE_PERMISSIONS 0660

#define VpcmAudioDevice org_vpcm_driver_VpcmAudioDevice

class VpcmAudioDevice : public IOAudioDevice, public DevfsDeviceNode
{
public:
  OSDeclareDefaultStructors( VpcmAudioDevice )
  
protected:
  virtual bool init( OSDictionary* );
  virtual void free();
  virtual bool initHardware( IOService* );

protected:
  virtual int devOpen( int );
  virtual int devClose();
  virtual int devRead( struct uio* );
  virtual int devWrite( struct uio* );

private:
  int executeCommand( int*, char** );
  int printInfo( int, char** );
  int createEngine( int, char** );
  int deleteEngine( int, char** );
  int nameEngine( int, char** );
  int describeEngine( int, char** );
  int findEngine( const char* ) const;
  static int printEngineStatus( VpcmAudioEngine*, char*, int );

private:
  int mIOFlags;
  Synchronization::Mutex mMutex;
  char* mpOutputBuffer;
};

#endif // VPCM_AUDIO_DEVICE_H
