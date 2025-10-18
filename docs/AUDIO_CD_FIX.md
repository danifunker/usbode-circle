# Audio CD Support Fix

## Problem Summary
Audio CDs were appearing as "blank DVD" on macOS and could not be played back. The issue had multiple causes:

## Root Causes Identified

### 1. **READ CAPACITY Block Size (CRITICAL)**
**Location**: `usbcdgadget.cpp` line 1082

**Problem**: READ CAPACITY was hardcoded to return 2048-byte blocks for all media types:
```cpp
TUSBCDReadCapacityReply m_ReadCapReply{
    htonl(0x00),
    htonl(2048)  // HARDCODED - wrong for audio CDs!
};
```

**Impact**: 
- macOS uses READ CAPACITY to determine media type
- Audio CDs use **2352-byte sectors** (RAW format with subchannel data)
- Data CDs use **2048-byte sectors** (cooked format, user data only)
- Reporting wrong block size confused macOS's optical drive detection

**Fix**: Made READ CAPACITY dynamically return block size based on disc type:
```cpp
int mediumType = GetMediumType();
u32 blockSize = 2048;  // Default for data CDs

if (mediumType == 0x02) {  // Pure audio CD
    blockSize = 2352;  // RAW audio sector size
}

m_ReadCapReply.nSectorSize = htonl(blockSize);
```

### 2. **TOC Lead-Out Control Byte**
**Location**: `usbcdgadget.cpp` line 1298

**Problem**: Lead-out track control byte was hardcoded to 0x14 (data track):
```cpp
tocEntries[index].ADR_Control = 0x14;  // ALWAYS data track
```

**Impact**: 
- Audio CD has tracks with control byte 0x10 (audio)
- But lead-out had 0x14 (data)
- Mixed signals confused macOS

**Fix**: Lead-out now matches disc type:
```cpp
int mediumType = GetMediumType();
if (mediumType == 0x02) {  // Audio CD
    tocEntries[index].ADR_Control = 0x10;  // Audio control
} else {
    tocEntries[index].ADR_Control = 0x14;  // Data control
}
```

### 3. **READ(10) Command for Audio Tracks**
**Location**: `usbcdgadget.cpp` line 1108

**Problem**: READ(10) was allowing reads of audio tracks with 2048-byte transfers:
```cpp
transfer_block_size = 2048;  // WRONG for audio!
```

**Impact**: 
- Per MMC-6 spec, READ(10) is **INVALID** for audio tracks
- Audio sectors are 2352 bytes, not 2048 bytes
- Hosts should use READ CD (0xBE) for audio instead
- Allowing this caused data corruption

**Fix**: READ(10) now rejects audio track reads:
```cpp
CUETrackInfo trackInfo = GetTrackInfoForLBA(m_nblock_address);
if (trackInfo.track_mode == CUETrack_AUDIO) {
    setSenseData(0x05, 0x64, 0x00);  // Illegal mode for this track
    sendCheckCondition();
    break;
}
```

## Why These Fixes Work Together

### The Block Size Detection Chain
1. **Host queries READ CAPACITY** → Gets 2352 bytes for audio CDs ✅
2. **Host sees 2352-byte blocks** → Recognizes as audio CD format ✅
3. **Host reads TOC** → Sees consistent audio control bytes (0x10) ✅
4. **Host tries READ(10)** → Gets error, switches to READ CD (0xBE) ✅
5. **Host uses READ CD (0xBE)** → Gets proper 2352-byte audio sectors ✅

### Without These Fixes
1. **Host queries READ CAPACITY** → Gets 2048 bytes ❌
2. **Host sees 2048-byte blocks** → Thinks it's data CD ❌
3. **Host reads TOC** → Sees audio tracks (0x10) but data leadout (0x14) ❌ **CONFUSED**
4. **Host unsure of media type** → Shows "blank DVD" ❌
5. **Audio playback fails** → Wrong block size, wrong commands ❌

## CD-ROM Block Size Reference

| CD Type | Block Size | Content | Commands |
|---------|-----------|---------|----------|
| Data CD (Mode 1) | 2048 bytes | User data only | READ(10), READ(12) |
| Audio CD (CD-DA) | 2352 bytes | RAW audio + subchannel | READ CD (0xBE) only |
| Mixed Mode | Varies by track | Data + Audio | Both commands, track-dependent |

### 2352-Byte Audio Sector Layout
```
[0-2047]    Main channel data (2048 bytes of audio samples)
[2048-2351] Subchannel data (304 bytes: P,Q,R,S,T,U,V,W subchannels + error correction)
```

### Why Audio CDs Need 2352 Bytes
- **2048 bytes** = Audio sample data (L+R stereo, 16-bit, 44.1kHz)
- **304 bytes** = Subchannel data including:
  - Q subchannel: Track position, timing, CD-TEXT
  - P subchannel: Track start/end markers
  - Error detection and correction codes

## Testing Checklist

### Before Testing
- [ ] Build with RASPPI=3 (or your Pi model)
- [ ] Have audio CD image ready (.cue + .bin format)
- [ ] Verify image has TRACK TYPE AUDIO in .cue file

### Test 1: Audio CD Detection
1. Mount audio CD image
2. Check macOS System Information
3. **Expected**: Shows as "CD-ROM" or "Audio CD", NOT "blank DVD"
4. **Expected**: Block size shows 2352 bytes (might show translated size)

### Test 2: TOC Reading
1. Open Disk Utility on macOS
2. Select the USBODE device
3. **Expected**: Shows track list for audio CD
4. **Expected**: No "blank DVD" or "unreadable" errors

### Test 3: Audio Playback
1. Try playing audio tracks in Music.app or Finder
2. **Expected**: Tracks play correctly
3. **Expected**: Track timing/position works
4. **Expected**: Skip forward/backward works

### Test 4: Data CD Still Works
1. Mount data CD (.iso file)
2. **Expected**: Shows as "CD-ROM" with 2048-byte blocks
3. **Expected**: Files readable
4. **Expected**: No regression from audio CD fix

### Test 5: Mixed-Mode CD
1. Mount mixed-mode CD (data + audio tracks)
2. **Expected**: Data tracks readable (2048 bytes)
3. **Expected**: Audio tracks playable (2352 bytes)
4. **Expected**: System handles both track types

## Potential Issues and Solutions

### Issue: Data CDs Stop Working
**Symptom**: Data CDs show as audio CDs or become unreadable

**Cause**: GetMediumType() returning wrong type

**Debug**:
```bash
# Check logs for:
"READ CAPACITY for AUDIO CD - returning 2352 byte blocks"  # Should only appear for audio CDs
"READ CAPACITY for DATA CD - returning 2048 byte blocks"   # Should appear for data CDs
```

**Solution**: Review GetMediumType() logic - it checks if track 1 is audio

### Issue: Audio CD Still Shows as Blank DVD
**Symptom**: macOS still doesn't recognize audio CD

**Possible Causes**:
1. Image file not true audio CD format (check .cue file)
2. macOS cache not cleared (try different USB port or reboot Mac)
3. VID/PID database override (unlikely after these fixes)

**Debug**:
```bash
# On Mac, check what READ CAPACITY returned:
diskutil info /dev/disk4  # replace disk4 with your device
# Look for "Device Block Size"
```

### Issue: READ CD (0xBE) Not Being Called
**Symptom**: Logs show READ(10) being rejected but nothing after

**Cause**: Host not falling back to READ CD

**Solution**: This is expected behavior - host should try READ CD automatically. If not:
- Check if READ CD (0xBE) command is in supported commands list
- Verify GET CONFIGURATION advertises CD-Read feature

### Issue: Wrong Track Type Detection
**Symptom**: Mixed-mode CD has wrong tracks identified

**Cause**: GetTrackInfoForLBA() returning wrong track info

**Debug**: Enable logging in GetTrackInfoForLBA() to see which track is selected for each LBA

## Implementation Notes

### Why Not Change READ(10) to Support Audio?
**Answer**: Per MMC-6 specification, READ(10) is **explicitly invalid** for audio tracks. Supporting it would:
- Violate the SCSI MMC standard
- Allow incorrect command usage
- Hide bugs where hosts use wrong commands
- Potentially break compatibility with other software

The proper behavior is:
- **Data tracks**: Use READ(10) or READ(12) → 2048 bytes
- **Audio tracks**: Use READ CD (0xBE) → 2352 bytes
- **Mixed discs**: Host selects command based on track type

### Why Dynamic Block Size in READ CAPACITY?
**Answer**: Because audio CDs and data CDs have fundamentally different block sizes:
- Audio CD: 2352 bytes (RAW format, includes subchannel)
- Data CD: 2048 bytes (cooked format, user data only)

macOS and other OSes use READ CAPACITY to determine media type and set up their I/O pipeline with the correct block size.

### Alternative Approach: Always Return 2048?
**Rejected** because:
- Violates MMC specification for audio CDs
- Causes macOS to misdetect audio CDs as data CDs
- Breaks audio playback (can't get full 2352-byte sectors)
- Forces workarounds in higher-level code

The MMC-compliant approach is to:
1. Return correct block size for media type
2. Force hosts to use correct commands for each track type
3. Let OS handle translation if needed

## References

### SCSI MMC-6 Specification
- **Section 6.1.5**: READ(10) command - "Valid only for data tracks"
- **Section 6.1.9**: READ CD command - "Required for audio tracks"
- **Section 6.19**: READ CAPACITY - "Returns logical block size"

### Red Book (CD-DA Standard)
- Audio CD sector format: 2352 bytes total
- Sample rate: 44.1 kHz, 16-bit stereo
- Subchannel data: 8 subchannels (P through W)

### macOS IOKit
- IOCDMedia: Media object for CD-ROM
- IOCDAudioControl: Audio CD playback control
- Block size detection: Uses READ CAPACITY result

## Summary

These three fixes work together to properly identify and handle audio CDs:

1. **READ CAPACITY** returns correct 2352-byte block size → macOS recognizes audio format
2. **TOC lead-out** matches disc type → consistent audio CD signaling  
3. **READ(10) rejection** for audio → forces correct command usage (READ CD)

Result: **Audio CDs should now be properly detected and playable on macOS** ✅
