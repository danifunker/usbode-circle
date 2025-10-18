# macOS Optical Drive Detection Investigation

## Problem Statement
macOS shows USBODE as `Protocol: Disk Image` instead of `Protocol: Optical Media`, causing:
- Wrong device icon (DVD icon for all media)
- Block size reported as 512 bytes instead of 2048/2352 bytes  
- Device not listed in `drutil`
- Audio CDs appear as "blank DVD"

## Current USB Configuration

### Device Descriptors
```c
bDeviceClass:    0x00  (defined in interface)
bDeviceSubClass: 0x00
bDeviceProtocol: 0x00
```

### Interface Descriptors
```c
bInterfaceClass:    0x08  (Mass Storage)
bInterfaceSubClass: 0x06  (SCSI MMC for CD-ROM) ✅ CORRECT
bInterfaceProtocol: 0x50  (Bulk-Only Transport)
```

### SCSI INQUIRY Response
```c
Peripheral Device Type: 0x05  (CD/DVD) ✅ CORRECT
RMB: 0x80 (Removable) ✅ CORRECT
Version: 0x05 (SPC-3)
```

## Comparison with Real CD Drive

| Attribute | Real CD Drive | USBODE | Status |
|-----------|---------------|---------|--------|
| Protocol | SATA | Disk Image | ❌ WRONG |
| Device Type | Optical Media | Generic | ❌ WRONG |
| Block Size | 2352 bytes | 512 bytes | ❌ WRONG |
| Optical Type | CD-ROM | (none) | ❌ MISSING |
| Icon | CD icon | DVD icon | ❌ WRONG |
| drutil listing | Yes | No | ❌ MISSING |

## Root Cause Analysis

### Why macOS Categorizes as "Disk Image"
macOS IOKit uses multiple factors to determine device category:

1. **USB Descriptors** - USBODE uses correct SubClass 0x06 ✅
2. **SCSI Command Responses** - INQUIRY reports CD/DVD (0x05) ✅
3. **Block Size Consistency** - macOS expects:
   - Data CD: 2048 bytes/block
   - Audio CD: 2352 bytes/block (RAW audio)
   - USBODE reports: 2048 bytes (via READ CAPACITY) ✅
   - macOS shows: 512 bytes ❌ **PROBLEM**

4. **Media State During Enumeration** - Unknown if media must be present
5. **IOKit Driver Matching** - May have hardcoded VID/PID database

### Block Size Mystery
READ CAPACITY correctly sends `htonl(2048)`, but macOS reports 512 bytes. This suggests:
- macOS is applying a **translation layer**
- Device is being matched by **IODVDBlockStorageDriver** instead of native optical driver
- Block size 512 is macOS's **default for generic mass storage**

### The "Disk Image" Protocol
When macOS shows `Protocol: Disk Image`, it means:
- Device is being handled by **disk image driver** (like mounting a .dmg file)
- NOT being handled by optical media driver
- This happens when IOKit can't confirm it's a "real" optical drive

## Potential Fixes

### 1. Fix Audio CD Lead-Out Control Byte ✅ IMPLEMENTED
**Status**: Fixed in this commit
**Impact**: Prevents audio CDs from showing as "blank DVD"

The lead-out track control byte was hardcoded to 0x14 (data track), confusing macOS when all other tracks were 0x10 (audio).

```cpp
// OLD: Always 0x14
tocEntries[index].ADR_Control = 0x14;  // Data track control for leadout

// NEW: Match disc type
int mediumType = GetMediumType();
if (mediumType == 0x02) {  // Audio CD
    tocEntries[index].ADR_Control = 0x10;  // Audio control
} else {
    tocEntries[index].ADR_Control = 0x14;  // Data control  
}
```

### 2. Report Audio CD Block Size as 2352 Bytes
**Status**: NOT IMPLEMENTED  
**Risk**: High - might break data CD reading
**Potential Impact**: Could trigger optical media detection

Audio CDs use RAW sector format (2352 bytes = 2048 data + 304 subchannel/error correction). Current implementation reports 2048 bytes for all CDs.

**Challenge**: Need to:
- Detect if disc is pure audio CD
- Change `m_ReadCapReply.nBlockSize` to `htonl(2352)` for audio CDs
- Ensure data reads still work correctly
- Handle mixed-mode CDs (both data and audio tracks)

### 3. Implement READ CD Command (0xBE)
**Status**: NOT IMPLEMENTED
**Risk**: Medium
**Potential Impact**: High - this is the "smoking gun" for optical drives

READ CD (0xBE) is the MMC command for reading CD sectors with subchannel data. Real optical drives support this, USB flash drives don't.

**Required**: Implement READ CD command with:
- Sector type selection (any/CDDA/data)
- Main channel selection (2352 bytes, 2048 bytes, etc.)
- Sub-channel selection (none/raw/Q)

This might be the key differentiator macOS uses.

### 4. Change VID/PID to Real Optical Drive
**Status**: TESTED - didn't help alone
**Impact**: Minimal by itself

Tried changing VID/PID but macOS still showed "Disk Image". The VID/PID database may require **combination** of correct VID/PID + correct command responses.

### 5. Implement Additional MMC Commands
**Status**: NOT IMPLEMENTED  
**Commands to Consider**:
- GET EVENT STATUS NOTIFICATION (0x4A) - Currently stubbed
- READ DISC INFORMATION (0x51) - Currently returns minimal data
- READ TRACK INFORMATION (0x52) - Not implemented
- READ CD (0xBE) - **Critical for audio CD**
- READ CD MSF (0xB9) - Audio playback

### 6. USB Protocol Change: Bulk-Only → CBI  
**Status**: NOT TESTED
**Risk**: Very High - would require major rewrite
**Impact**: Unknown

Some older optical drives used Control/Bulk/Interrupt (CBI) protocol (0x00 or 0x01) instead of Bulk-Only (0x50). However:
- Most modern USB optical drives use Bulk-Only (0x50)
- Changing this would require rewriting endpoint handling
- Low probability of success
- **Not recommended without evidence**

## Recommended Next Steps

### Phase 1: Audio CD Fix ✅ DONE
1. Fix lead-out control byte to match disc type
2. Test with audio CD image on macOS

### Phase 2: Audio CD Block Size (Optional)
1. Implement dynamic block size reporting:
   - 2352 bytes for pure audio CDs
   - 2048 bytes for data/mixed CDs
2. Test if macOS recognizes as optical media
3. Risk: Might break data CD mounting

### Phase 3: READ CD Command (Recommended)
1. Implement READ CD (0xBE) command
2. Support main channel selection (2048/2352 bytes)
3. Support sub-channel Q for CD-TEXT
4. This is likely the **key differentiator** macOS uses

### Phase 4: Accept Current Behavior (Pragmatic)
If fixes don't work:
- **Disc mounting works** ✅
- **Data is readable** ✅  
- **Files are accessible** ✅
- Only cosmetic issues remain:
  - Wrong icon (DVD instead of CD)
  - Wrong protocol label ("Disk Image")
  - Not in `drutil` list

**User impact**: Minimal - functionality is intact

## Linux Comparison Needed

The user suggested looking at **Linux f_mass_storage** implementation. This would show:
- How Linux kernel handles optical drive vs mass storage gadgets
- What USB descriptors are used
- What SCSI commands are required
- Whether protocol 0x50 (Bulk-Only) is correct

This research could provide definitive answers.

## References

### SCSI MMC Specification
- **MMC-6**: Multimedia Commands specification for CD/DVD drives
- **Key Commands**: READ CD (0xBE), GET CONFIGURATION, READ DISC INFORMATION

### macOS IOKit
- **IOSCSIPeripheralDeviceType05**: SCSI CD/DVD driver
- **IODVDServices**: DVD-specific services
- **IODVDBlockStorageDriver**: Block-level DVD access
- **IODVDMedia**: Media object representation

### USB Mass Storage Class
- **Bulk-Only Transport (BOT)**: Protocol 0x50 - most common
- **Control/Bulk/Interrupt (CBI)**: Protocol 0x00/0x01 - older drives
- **SubClass 0x06**: SCSI MMC (CD-ROM specific)

## Conclusion

The lead-out control byte fix will solve the **"blank DVD" issue for audio CDs**. However, the deeper **"Disk Image" protocol categorization** may require implementing READ CD (0xBE) command or accepting current behavior as working-but-imperfect.

The fact that:
- Data CDs mount and work correctly ✅
- Files are readable ✅
- DVD images work with `.dvd.iso` naming ✅

Suggests the core functionality is sound, and the remaining issues are macOS-specific detection quirks rather than fundamental problems.
