#ifndef VPCM_AUDIO_ENGINE_H
#define VPCM_AUDIO_ENGINE_H

#include <IOKit/audio/IOAudioEngine.h>
#include "DevfsDeviceNode.h"
#include "Synchronization.h"
#include "VpcmProperties.h"

extern "C" struct selinfo { char data[128]; }; // no actual declaration available, size guessed

class VpcmAudioEngine : public IOAudioEngine, public DevfsDeviceNode
{
    OSDeclareDefaultStructors( VpcmAudioEngine )
    
public:
    const VpcmProperties* getProperties() const { return &mProperties; }
    int getIOFlags() const;
  
// IOAudioEngine
    virtual bool init( const VpcmProperties* );
    virtual void free();

    virtual bool initHardware( IOService* );
    virtual bool terminate( IOOptionBits );

    virtual IOReturn performAudioEngineStart();
    virtual IOReturn performAudioEngineStop();
    virtual UInt32 getCurrentSampleFrame();

    virtual IOReturn stopAudioEngine();
    virtual void stopEngineAtPosition( IOAudioEnginePosition* );
    virtual void resetClipPosition( IOAudioStream*, UInt32 );

    virtual IOReturn eraseOutputSamples( const void*, void*, UInt32, UInt32, const IOAudioStreamFormat*, IOAudioStream* );
    virtual IOReturn clipOutputSamples( const void*, void*, UInt32, UInt32, const IOAudioStreamFormat*, IOAudioStream* );
    virtual IOReturn convertInputSamples( const void*, void*, UInt32, UInt32, const IOAudioStreamFormat*, IOAudioStream* );
  

private:
    void onBufferTimer( IOTimerEventSource* );
    IOReturn onControlChanged( IOAudioControl*, SInt32, SInt32 );
    int onDevOpen();
    int onDevClose();
  
private:
    int mGain, mVolume, mMuteInput, mMuteOutput;
    static const struct ControlDef
    { IOAudioControl* (*create)( int, int, int );
      int usage;
      int initialValue;
      int VpcmAudioEngine::* pValue;
    } sAudioControls[];

    union Time { AbsoluteTime a; uint64_t t; int64_t s; };
    Time mNextTime, mBufferDuration;
    IOTimerEventSource*	mpTimer;
    int mWritePosition;
  
// DevfsDeviceNode
protected:
    virtual int devOpen( int );
    virtual int devClose();
    virtual int devRead( struct uio* );
    virtual int devWrite( struct uio* );
    virtual int devIoctl( u_long, caddr_t );
    virtual int devSelect( int, void*, struct proc* );

private:
    int devReadWrite( struct uio* );

    VpcmProperties mProperties;
    union DataPtr { void* v; char* c; short* s; float* f; };
    struct Buffer
    {
      IOBufferMemoryDescriptor* pDesc;
      DataPtr begin, end;
      Buffer() : pDesc( 0 ) { begin.c = 0; end.c = 0; }
      bool init( int );
      void free();
      int bytes() { return int( end.c - begin.c ); }
    } mBuffer;
    DataPtr mDevIOPtr;
    int mDevIOBytesAvail;
    struct selinfo mDevIOSel;
    Synchronization::Mutex mDevIOWait;
    int mIOState;
};


#endif // AUDIO_ENGINE_H
