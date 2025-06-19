# SeaBIOS Error 0003 Diagnosis and Enhanced Logging

## Problem
The ITX Llama BIOS shows the error: **"Could not read from CDROM (code 0003)"** when trying to boot from the USBODE USB CD-ROM gadget.

## Root Cause Analysis

### SeaBIOS Error Code 0003
Based on analysis of the SeaBIOS source code (`src/cdrom.c`), error code 0003 specifically means:
- **Location**: `cdrom_boot()` function in SeaBIOS
- **Failure Point**: `ret = process_op(&dop);` returns failure 
- **Context**: Reading the Boot Record Volume Descriptor at **LBA 0x11 (sector 17)**

### SeaBIOS Boot Sequence
1. BIOS calls `cdrom_boot()` 
2. Checks if SCSI device is ready with `scsi_is_ready()`
3. **CRITICAL**: Reads Boot Record Volume Descriptor at LBA 0x11
4. Validates the descriptor contains "CD001\001EL TORITO SPECIFICATION"
5. Reads the boot catalog and loads the boot image

### Error Code Mapping
From SeaBIOS `cdrom_boot()` function:
- **Error 1**: Drive not found or not ready
- **Error 3**: Failed to read Boot Record Volume Descriptor at LBA 0x11 ← **OUR ISSUE**
- **Error 4**: Invalid Boot Record Volume Descriptor (first byte not 0)
- **Error 5**: Missing "CD001\001EL TORITO SPECIFICATION" signature
- **Error 7**: Failed to read boot catalog
- **Error 8-12**: Various boot catalog validation failures

## Enhanced Logging Implementation

### Key Diagnostic Points Added

1. **SCSI Command Logging**
   - Enhanced READ (10) command logging with BIOS-critical LBA identification
   - Special highlighting when LBA 0x11 (Boot Record Volume Descriptor) is requested
   - Full CDB (Command Descriptor Block) logging

2. **Device Ready State Tracking**
   - TEST UNIT READY command enhanced with BIOS boot context
   - CD ready state changes logged with boot impact warnings
   - Endpoint recreation logging (shows when device becomes not ready)

3. **Data Transfer Logging**
   - Detailed batch read logging in `Update()` function
   - First 32 bytes of LBA 0x11 data logged for validation
   - Read failure logging with SeaBIOS error 0003 context
   - Seek operation logging

4. **Error Response Logging**
   - Enhanced CSW (Command Status Wrapper) logging
   - REQUEST SENSE command logging with detailed error codes
   - Sense key, ASC, and ASCQ values logged for BIOS error diagnosis

5. **Critical Path Identification**
   - LBA range 0x10-0x12 marked as BIOS critical
   - Boot Record Volume Descriptor reads specifically identified
   - Failure modes linked to SeaBIOS error 0003

### Log Message Format
- **SUCCESS**: Normal log level for successful operations
- **FAILURE**: Error level with "*** CRITICAL FAILURE ***" prefix
- **BIOS CONTEXT**: Messages include "[BIOS CRITICAL]" or "THIS CAUSES SeaBIOS ERROR 0003"

## Debugging Workflow

### Step 1: Check Device Ready State
Look for these log messages:
```
CUSBCDGadget::SetDevice: *** CD NOW READY FOR BIOS BOOT ***
CUSBCDGadget::HandleSCSICommand: Test Unit Ready SUCCESS - CD ready for BIOS boot
```

### Step 2: Monitor LBA 0x11 Read Request
Look for:
```
CUSBCDGadget::HandleSCSICommand: *** BIOS BOOT CRITICAL *** Reading Boot Record Volume Descriptor at LBA 0x11
UpdateRead: Starting batch read for X blocks at LBA 17 - BIOS critical if LBA >= 0x10
```

### Step 3: Verify Data Content
Check for the Boot Record Volume Descriptor data:
```
UpdateRead: *** BIOS BOOT DATA *** LBA 0x11 (Boot Record Volume Descriptor) first 32 bytes: [hex dump]
```
- First byte should be 0x00
- Bytes 1-7 should contain "CD001\001EL"

### Step 4: Track Transfer Completion
Monitor for:
```
CUSBCDGadget::SendCSW: SUCCESS RESPONSE - status: 0, tag: 0x..., residue: 0
```

### Common Failure Modes

1. **Device Not Ready**
   ```
   *** Test Unit Ready FAILED *** - CD not ready, will cause BIOS boot failure
   ```

2. **Read Failure**
   ```
   *** CRITICAL READ FAILURE *** Partial read: requested X bytes, got Y bytes at LBA 17
   ```

3. **Seek Failure**
   ```
   *** CRITICAL FAILURE *** CD read failed: CDReady=ready, offset=18446744073709551615 at LBA 17
   ```

4. **Invalid Boot Descriptor**
   - Check the hex dump of LBA 0x11 data
   - Should start with `00` followed by `CD001` signature

## Next Steps

1. **Run with Enhanced Logging**: The enhanced logging will now provide detailed information about exactly where the failure occurs in the BIOS boot sequence.

2. **Analyze Boot Record**: If data is successfully read from LBA 0x11, verify it contains a valid El Torito boot record.

3. **Compare with Working System**: Compare the logged data from LBA 0x11 with a known working bootable CD-ROM image.

4. **Hardware/Timing Issues**: If reads are failing, may need to investigate USB timing, endpoint stability, or disc image integrity.

## File Changes Made

- **usbcdgadget.cpp**: Enhanced logging throughout SCSI command handling, data reading, and error reporting
- All logging changes are additive and don't affect functionality
- Can be easily disabled by changing `MLOGNOTE`/`MLOGERR` macros if needed

This enhanced logging should provide clear visibility into exactly why SeaBIOS is generating error code 0003 and help identify the specific failure point in the boot sequence.
