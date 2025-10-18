# CD Audio Detection Debugging Session - Progress Report

**Date**: October 18, 2025  
**Branch**: bluescsi-cdrom-stuff-advanced  
**Status**: Reverted - USB SubClass change causes enumeration issues

## Original Problem
**Goal**: Support CD Digital Audio discs. Current implementation detected audio CDs as "blank DVD" instead of recognizing them as audio CDs.

## What We Learned from Physical Drive Analysis

### USB Capture from Samsung SE-S084 DVD Drive (via Wireshark)
**Key Findings:**
- **USB Interface SubClass**: `0x05` (SCSI Transparent Command Set)
- **Device Class**: Mass Storage Class (0x08)
- **Protocol**: Bulk-Only Transport (0x50)
- **Important**: Physical drive **NEVER receives READ TOC commands** when no disc is present
- Commands macOS sends when no disc: TEST UNIT READY, REQUEST SENSE, INQUIRY, GET CONFIGURATION, MODE SENSE

### macOS System Behavior
**Detection Pattern:**
- macOS loads `IODVDBlockStorageDriver` for optical drives
- Uses `DiskArbitration` framework to detect and mount media
- When SubClass = 0x06 (SCSI MMC): Detected as generic storage
- When SubClass = 0x05 (SCSI Transparent): Detected as optical drive BUT causes USB enumeration issues

**Diagnostic Commands Used:**
```bash
# View macOS system logs in real-time
tail -f /var/log/system.log | grep -i "usb\|disk"

# Check what drivers are loaded
kextstat | grep -i "storage\|dvd\|cd"

# List optical drives
drutil list

# Disk utility info
diskutil list
diskutil info /dev/disk1

# View USBODE logs
tail -f /Volumes/bootfs/usbode-logs.txt

# Search for specific patterns in logs
grep "READ TOC" /Volumes/bootfs/usbode-logs.txt
grep "Transfer complete" /Volumes/bootfs/usbode-logs.txt
grep -A 2 "Begin Transfer In  nlen= 4" /Volumes/bootfs/usbode-logs.txt
```

## Changes We Attempted (in chronological order)

### 1. ✅ USB Interface SubClass Change (0x06 → 0x05)
**File**: `addon/usbcdgadget/usbcdgadget.cpp` lines 78-88

**Change**: 
```cpp
// Before: 0x08, 0x06, 0x50 (SCSI MMC CD-ROM)
// After:  0x08, 0x05, 0x50 (SCSI Transparent)
```

**Result**: 
- ✅ macOS now loads `IODVDBlockStorageDriver` 
- ✅ System.log shows: "USBMSC Identifier: USBODE-45D55B29"
- ❌ But introduced USB enumeration problems (bus resets after READ TOC)

### 2. ❌ READ TOC DataLength Field Correction
**What we tried**: Updated the DataLength field in the TOC header when truncating response

```cpp
// When host requests 4 bytes but we have 20 bytes:
u16 correctedLength = htons(actualDataLength - 2);
memcpy(m_InBuffer, &correctedLength, 2);
```

**Rationale**: MMC spec says DataLength = total length - 2 bytes

**Result**: Field was set correctly, but didn't solve the transfer completion issue

### 3. ❌ Minimum Transfer Size Padding (4 → 12 bytes)
**What we tried**: Padded small READ TOC transfers to 12 bytes minimum

```cpp
const int MIN_TOC_SIZE = 12;
if (usbTransferLength < MIN_TOC_SIZE) {
    memset(m_InBuffer + usbTransferLength, 0, MIN_TOC_SIZE - usbTransferLength);
    usbTransferLength = MIN_TOC_SIZE;
}
```

**Rationale**: Suspected Circle framework had issues with transfers < 8 bytes

**Result**: Transfer still didn't complete; macOS still reset the USB bus

### 4. ❌ Manual Cache Flushing
**What we tried**: Explicitly flushed ARM data cache for small transfers

```cpp
if (datalen < 8) {
    CleanAndInvalidateDataCacheRange((uintptr)m_InBuffer, 32);
}
```

**Rationale**: ARM DMA requires cache coherency; maybe automatic flush wasn't working

**Result**: No change in behavior

### 5. ❌ CSW Residue Management
**What we tried**: 
- First: Set `m_CSW.dCSWDataResidue` before `BeginTransfer`
- Then: Removed residue setting (let it stay at default 0)

```cpp
// Tried: m_CSW.dCSWDataResidue = m_CBW.dCBWDataTransferLength - actualDataLength;
// Then removed this line entirely
```

**Rationale**: READ TOC was the only command setting residue before transfer; thought this might confuse Circle framework

**Result**: No change in behavior

### 6. ❌ Simplified Implementation (Remove All Special Handling)
**What we tried**: Made READ TOC work exactly like READ CAPACITY and MODE SENSE

```cpp
// Just truncate if needed, no padding, no special handling
if (allocationLength < datalen) {
    datalen = allocationLength;
}
m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, m_InBuffer, datalen);
m_nState = TCDState::DataIn;
m_CSW.bmCSWStatus = bmCSWStatus;
```

**Rationale**: Working commands don't do anything special, so READ TOC shouldn't either

**Result**: Still didn't complete; macOS still reset USB bus

## The Persistent Symptom

### What We Consistently Observed:
```
[timestamp] [CDEndpoint] NOTICE: Begin Transfer In  nlen= 4
[timestamp] [CDEndpoint] NOTICE: Transfer In data (first %zu bytes): 
[timestamp] [CUSBCDGadget::GetDescriptor] NOTICE: entered  ← USB BUS RESET!
```

**Critical Pattern:**
- READ TOC calls `BeginTransfer()` successfully
- USB transfer starts
- **NO "Transfer complete" message** - `OnTransferComplete()` never fires
- macOS immediately resets the USB bus (GetDescriptor called, endpoints reactivated)
- macOS system.log: "AppleUSBEHCI::Found a transaction which hasn't moved in 5000 milliseconds"

### Working Commands (for comparison):
```
[timestamp] [CDEndpoint] NOTICE: Begin Transfer In  nlen= 8  ← READ CAPACITY
[timestamp] [CDEndpoint] NOTICE: Transfer complete nlen= 8  ✅
[timestamp] [onXferCmplt] NOTICE: DataIn complete, calling SendCSW
```

## Technical Analysis

### Why Transfers Complete:
1. `BeginTransfer()` sets up DMA transfer in Circle's USB gadget driver
2. USB hardware raises `XFER_COMPLETE` interrupt when done
3. `HandleInInterrupt()` detects interrupt, calls `OnTransferComplete()`
4. State machine advances, sends CSW

### Why READ TOC Transfers Don't Complete:
**Hypothesis**: The USB hardware never raises the `XFER_COMPLETE` interrupt for READ TOC transfers, causing them to hang indefinitely. macOS detects the stalled transaction and resets the bus.

**Possible Root Causes:**
1. **SubClass 0x05 expectations**: macOS expects specific behavior from SCSI Transparent devices that we're not meeting
2. **READ TOC data format**: Despite matching the spec, something about our response causes macOS to abort
3. **Circle framework bug**: Specific combination of transfer size/data pattern triggers Circle USB gadget driver issue
4. **USB protocol violation**: Some subtle BOT protocol issue that only manifests with READ TOC

## Comparison: Physical Drive vs USBODE

| Aspect | Physical Drive | USBODE (SubClass 0x06) | USBODE (SubClass 0x05) |
|--------|---------------|------------------------|------------------------|
| SubClass | 0x05 | 0x06 | 0x05 |
| Enumeration | ✅ Success | ✅ Success | ❌ Bus resets |
| IODVDBlockStorageDriver | ✅ Loads | ❌ Doesn't load | ✅ Loads |
| READ TOC (no disc) | Never sent | Sent & fails | Sent & causes reset |
| Detection in drutil | ✅ Shows up | ❌ Doesn't show | ❌ Doesn't show |

## What We Know Works

### Confirmed Working in Current Code:
- ✅ USB enumeration (with SubClass 0x06)
- ✅ INQUIRY command
- ✅ TEST UNIT READY command
- ✅ REQUEST SENSE command
- ✅ READ CAPACITY command (8-byte transfer)
- ✅ MODE SENSE commands (0, 8, 18 byte transfers)
- ✅ CSW (Command Status Wrapper) transfers (13 bytes)
- ✅ All transfers from 0 to 18 bytes (except READ TOC!)

## Key Insights

### Critical Discoveries:
1. **Transfer size is NOT the issue**: 4-byte, 8-byte, 12-byte, 18-byte transfers all work for other commands
2. **SCSI protocol compliance is NOT the issue**: READ TOC data format matches MMC spec and physical drive
3. **It's something specific to READ TOC**: Same buffer, same endpoint, same transfer mechanism - only READ TOC fails
4. **SubClass change creates new problems**: While 0x05 enables optical drive detection, it also causes USB instability

### Physical Drive Pattern:
- macOS **doesn't send READ TOC** to physical drive when no disc present
- But USBODE **receives READ TOC** from macOS even with no disc
- This suggests macOS treats SubClass 0x05 differently and expects different command sequences

## Recommended Next Steps

### Revert Strategy:
```bash
# View recent commits to find pre-SubClass change
git log --oneline -20

# Look for these changes to revert:
# - USB Interface SubClass change (0x06 → 0x05)
# - All READ TOC padding/truncation logic
# - CSW residue handling changes
# - Cache flushing workarounds

# Revert to known stable point
git checkout <commit-hash>
```

### Alternative Approaches to Try:

#### Option 1: Keep SubClass 0x06, Improve Audio CD Detection
1. **Keep SubClass 0x06**, focus on making READ TOC return correct data for audio CDs
2. **Implement proper audio CD detection** in READ CAPACITY and MODE SENSE responses
3. **Study how real optical drives respond** when they detect audio vs data discs
4. **Test with actual audio CD image** (e.g., FF_CD.bin) to see if problem is "no disc" vs "audio disc" detection

#### Option 2: Investigate Circle Framework
1. **Check if macOS needs specific mode pages** for optical drive recognition with SubClass 0x06
2. **Examine Circle's dwusbgadgetendpoint.cpp** to understand why READ TOC transfers don't complete
3. **Look for Circle framework bugs** or patches related to USB gadget transfers
4. **Test with different Circle versions** to see if this is a known issue

#### Option 3: Different USB Protocol Approach
1. **Study USB Mass Storage BOT specification** for edge cases with small transfers
2. **Compare USB packet captures** between physical drive and USBODE for READ TOC
3. **Implement USB packet-level logging** in Circle to see what's happening on the wire
4. **Check if there's a timing issue** with how quickly we respond to READ TOC

### Files Modified During Session:
- `addon/usbcdgadget/usbcdgadget.cpp` (lines ~78-88: USB SubClass descriptor)
- `addon/usbcdgadget/usbcdgadget.cpp` (lines ~1300-1470: READ TOC implementation)
- `addon/usbcdgadget/usbcdgadget.cpp` (line 30: added `#include <circle/synchronize.h>`)

### Changes to Revert:
```cpp
// File: addon/usbcdgadget/usbcdgadget.cpp, line ~85
// Revert this:
0x08, 0x05, 0x50  // SubClass 0x05 (SCSI Transparent)
// Back to:
0x08, 0x06, 0x50  // SubClass 0x06 (SCSI MMC CD-ROM)

// File: addon/usbcdgadget/usbcdgadget.cpp, line 30
// Can remove if not needed elsewhere:
#include <circle/synchronize.h>

// File: addon/usbcdgadget/usbcdgadget.cpp, lines ~1410-1470
// Revert READ TOC implementation to simple version without:
// - DataLength correction logic
// - Minimum transfer size padding
// - Manual cache flushing
// - CSW residue handling
```

## Conclusion

The SubClass change from 0x06 to 0x05 successfully makes macOS load the optical drive driver but introduces critical USB stability issues specifically with READ TOC commands. Despite multiple attempts to fix the READ TOC implementation (data format, transfer sizes, padding, cache coherency), the fundamental issue persists: macOS resets the USB bus before the READ TOC transfer can complete.

**Root Cause**: The USB hardware never raises the `XFER_COMPLETE` interrupt for READ TOC transfers when SubClass is set to 0x05, causing the transfer to hang indefinitely until macOS times out and resets the bus.

**Recommendation**: Revert to SubClass 0x06 and pursue optical drive detection through proper SCSI response data rather than USB descriptor changes. The "blank DVD" detection issue is likely solvable through SCSI protocol responses without requiring the problematic SubClass change.

## Related Documentation
- [DVD_SUPPORT.md](DVD_SUPPORT.md) - DVD support documentation
- [AUDIO_CD_FIX.md](docs/AUDIO_CD_FIX.md) - Audio CD fix documentation
- [MACOS_OPTICAL_DRIVE_DETECTION.md](docs/MACOS_OPTICAL_DRIVE_DETECTION.md) - macOS detection details
