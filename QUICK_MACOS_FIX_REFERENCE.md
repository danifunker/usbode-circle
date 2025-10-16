# Quick MacOS Fix Reference

## The Core Problem
MacOS requires very specific SCSI Unit Attention state handling for media change detection. Without proper state management, MacOS won't recognize media changes or will get stuck in eject/insert loops.

## The Solution: Media State Machine

### State Flow
```
NO_MEDIUM (no disc)
    ↓ (insert media)
MEDIUM_PRESENT_UNIT_ATTENTION (disc inserted, needs attention)
    ↓ (REQUEST SENSE delivered)
MEDIUM_PRESENT_READY (disc ready for use)
```

## Key Changes Summary

| Change | File | Impact | Why for MacOS |
|--------|------|--------|---------------|
| **MediaState enum** | usbcdgadget.h | High | MacOS needs proper Unit Attention state tracking |
| **setSenseData() helpers** | usbcdgadget.cpp | Medium | Consistent error reporting MacOS expects |
| **Enhanced REQUEST SENSE** | usbcdgadget.cpp | **Critical** | Proper state transition after Unit Attention |
| **Enhanced GET EVENT STATUS** | usbcdgadget.cpp | **Critical** | MacOS polls constantly for media changes |
| **Enhanced READ TOC** | usbcdgadget.cpp | High | ADR/Control bytes must be correct for MacOS |
| **Enhanced DISC INFO** | usbcdgadget.cpp | Medium | Proper disc type identification |
| **Enhanced SetDevice()** | usbcdgadget.cpp | **Critical** | Triggers proper media change sequence |

## Most Critical Fix: REQUEST SENSE State Transition

### Before (Broken on MacOS)
```cpp
if (m_SenseParams.bSenseKey == 0x02) {
    // After "Not Ready", always go to Unit Attention
    m_SenseParams.bSenseKey = 0x06;
}
```

### After (MacOS Compatible)
```cpp
if (m_mediaState == MediaState::MEDIUM_PRESENT_UNIT_ATTENTION) {
    // After delivering Unit Attention, transition to Ready
    m_mediaState = MediaState::MEDIUM_PRESENT_READY;
    clearSenseData();
}
```

**Why:** MacOS expects Unit Attention to be delivered once, then cleared. The old code kept returning Unit Attention forever.

## Quick Test

### 1. Build
```bash
make RASPPI=4 dist-single
```

### 2. Deploy to SD Card
Copy `dist64/` contents to SD card root

### 3. Test on Mac
1. Connect USBODE to Mac
2. Mount ISO via web interface
3. **Should see:** Mac automatically mounts disc
4. **Should not see:** Repeated eject/insert notifications

### 4. Check Logs
```bash
# On USBODE terminal
tail -f /var/log/usbode.log
```

Look for:
- "Moving sense state from Unit Attention to Ready" ✓
- "Get Event Status Notification - sending NewMedia event" ✓

## Common MacOS Issues and Fixes

| Symptom | Likely Cause | Fix Applied |
|---------|--------------|-------------|
| Disc not detected | Unit Attention not sent | Enhanced SetDevice() |
| Repeated eject/insert | Event Status wrong | Enhanced GET EVENT STATUS |
| Wrong disc type shown | Bad TOC ADR/Control | Enhanced READ TOC |
| Mount fails | Bad disc info | Enhanced DISC INFORMATION |

## If MacOS Still Has Issues

1. **Enable debug logging**
   ```cpp
   #define MLOGDEBUG(From, ...) CLogger::Get()->Write(From, LogDebug, __VA_ARGS__)
   ```

2. **Check MacOS Console.app**
   - Filter for "IOSCSIPeripheralDeviceType05"
   - Look for SCSI errors

3. **Compare with working drive**
   - Test same ISO on real CD-ROM
   - Note any behavioral differences

## Technical Details

### Media State Transitions
```
SetDevice(nullptr) → NO_MEDIUM
    Sense: 0x02/0x3A/0x00 (Not Ready, Medium Not Present)

SetDevice(new_device) → MEDIUM_PRESENT_UNIT_ATTENTION
    Sense: 0x06/0x28/0x00 (Unit Attention, Medium May Have Changed)
    discChanged = true

REQUEST SENSE → MEDIUM_PRESENT_READY
    Sense: 0x00/0x00/0x00 (No Sense)
    discChanged may be cleared by GET EVENT STATUS
```

### ADR/Control Byte Format
```
Bits 7-4: ADR (Address encoding)
    1 = Q subchannel mode-1 (position)

Bits 3-0: Control (Track characteristics)
    0 = 2 audio channels, no pre-emphasis  → Full byte: 0x10
    4 = Data track, digital copy allowed   → Full byte: 0x14
```

### GET EVENT STATUS Event Codes
```
0x00 = No Change
0x02 = New Media (after insertion)
0x03 = Media Removal (after ejection)
```

## Verification Checklist

- [ ] Code builds without errors
- [ ] MacOS detects inserted media automatically
- [ ] MacOS shows correct disc type (audio vs data)
- [ ] MacOS can eject and reinsert different discs
- [ ] No repeated eject/insert notifications
- [ ] Logs show proper state transitions
- [ ] Audio CDs show tracks in Music app (if applicable)
- [ ] Data discs mount and files are readable

## Roll Back

If issues occur, revert these files:
```bash
git checkout HEAD -- addon/usbcdgadget/usbcdgadget.h
git checkout HEAD -- addon/usbcdgadget/usbcdgadget.cpp
```

## Additional Resources

- Full documentation: `MACOS_COMPATIBILITY_CHANGES.md`
- BlueSCSI reference: https://github.com/BlueSCSI/BlueSCSI-v2
- SCSI sense codes: https://www.t10.org/lists/asc-num.htm

---
**TL;DR:** Media state machine + proper Unit Attention handling = MacOS compatibility
