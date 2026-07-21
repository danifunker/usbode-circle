//
// Host-build stub for <cdplayer/cdplayer.h>.
// An instrumented fake: the PlayState enum and the method signatures match
// the real CCDPlayer, but every call is recorded so tests can assert what
// the SCSI layer asked the player to do (e.g. that PLAY AUDIO MSF reaches
// Play() with the right LBA range), and tests can preset the state the
// player reports back (for READ SUB-CHANNEL).
//
#ifndef _cdplayer_cdplayer_h
#define _cdplayer_cdplayer_h

#include <circle/sched/task.h>
#include <circle/types.h>
#include <discimage/imagedevice.h>

class CCDPlayer : public CTask
{
public:
    enum PlayState
    {
        PLAYING,
        SEEKING,
        SEEKING_PLAYING,
        STOPPED_OK,
        STOPPED_ERROR,
        PAUSED,
        NONE
    };

    CCDPlayer(void) {}

    void EnsureAudioInitialized(void) { ensureAudioInitializedCalls++; }

    boolean SetDevice(IImageDevice *pDevice)
    {
        device = pDevice;
        setDeviceCalls++;
        return TRUE;
    }

    boolean Pause(void)
    {
        pauseCalls++;
        state = PAUSED;
        return TRUE;
    }

    boolean Resume(void)
    {
        resumeCalls++;
        state = PLAYING;
        return TRUE;
    }

    boolean SetVolume(u8 vol)
    {
        volume = vol;
        setVolumeCalls++;
        return TRUE;
    }

    u8 GetVolume(void) { return volume; }

    unsigned int GetState(void) { return state; }

    u32 GetCurrentAddress(void) { return currentAddress; }

    boolean Seek(u32 lba)
    {
        seekCalls++;
        lastSeekLBA = lba;
        currentAddress = lba;
        return TRUE;
    }

    boolean Play(u32 lba, u32 num_blocks)
    {
        playCalls++;
        lastPlayLBA = lba;
        lastPlayBlocks = num_blocks;
        currentAddress = lba;
        state = PLAYING;
        return TRUE;
    }

    // Test-visible call log and presettable state
    IImageDevice *device = nullptr;
    PlayState state = NONE;
    u32 currentAddress = 0;
    u8 volume = 255;

    int playCalls = 0;
    u32 lastPlayLBA = 0;
    u32 lastPlayBlocks = 0;
    int pauseCalls = 0;
    int resumeCalls = 0;
    int seekCalls = 0;
    u32 lastSeekLBA = 0;
    int setVolumeCalls = 0;
    int setDeviceCalls = 0;
    int ensureAudioInitializedCalls = 0;
};

#endif
