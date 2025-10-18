# DVD Support Implementation

## Overview
USBODE now supports both CD-ROM and DVD-ROM images with automatic detection based on filename.

## Usage

### DVD Images
To load a DVD image, use the `.dvd.iso` extension in your filename:
```
movie.dvd.iso
data_dvd.dvd.iso
```

### CD-ROM Images  
Any other `.iso` file or `.cue` file will be treated as CD-ROM:
```
game.iso
audio_cd.cue
data.iso
```

## How It Works

### Media Type Detection
When `SetDevice()` is called with an image filename:
1. **Checks for `.dvd.iso` extension** → Sets `MediaType::MEDIA_DVD`
2. **All other files** → Sets `MediaType::MEDIA_CDROM`

### Dynamic Capability Reporting
Mode Page 0x2A (MM Capabilities) byte 0 changes based on media type:

**CD-ROM Mode** (0x38 = 0011 1000):
- Bit 0: DVD-ROM Read = **0** (NO DVD)
- Bit 1: DVD-R Read = **0** (NO DVD)
- Bit 2: DVD-RAM Read = **0** (NO DVD)
- Bit 3: CD-R Read = **1** (YES)
- Bit 4: CD-RW Read = **1** (YES)
- Bit 5: Method 2 (multi-session) = **1** (YES)

**DVD Mode** (0x39 = 0011 1001):
- Bit 0: DVD-ROM Read = **1** (YES)
- Bit 1: DVD-R Read = **0** (NO - read only)
- Bit 2: DVD-RAM Read = **0** (NO - read only)
- Bit 3: CD-R Read = **1** (YES - backward compatible)
- Bit 4: CD-RW Read = **1** (YES - backward compatible)
- Bit 5: Method 2 (multi-session) = **1** (YES)

## Technical Details

### Code Changes

1. **Header** (`usbcdgadget.h`):
   - Added `MediaType` enum (MEDIA_NONE, MEDIA_CDROM, MEDIA_DVD)
   - Added `m_mediaType` member variable
   - Added `m_currentImageName[256]` to store filename
   - Updated `SetDevice()` signature to accept optional `imageName` parameter

2. **Implementation** (`usbcdgadget.cpp`):
   - Updated `SetDevice()` to store filename and detect media type
   - Modified Mode Sense (6) to dynamically set capability bits
   - Modified Mode Sense (10) to dynamically set capability bits

### MacOS Compatibility
- **CD-ROM images**: MacOS sees a CD-ROM drive, properly mounts data CDs and recognizes audio CDs
- **DVD images**: MacOS sees a DVD-ROM capable drive, mounts DVDs as expected
- No more "blank DVD" errors for CD images!

### Backward Compatibility
- Existing CD images continue to work without renaming
- CUE/BIN format continues to work (CD-ROM only, no DVD bin/cue support)
- No changes needed to existing workflows

## Implementation Notes

### Why .dvd.iso?
The `.dvd.iso` naming convention was chosen because:
1. **Simple and clear** - obvious what the file contains
2. **No ambiguity** - doesn't conflict with standard `.iso` extension
3. **Backward compatible** - existing `.iso` files default to CD-ROM
4. **No size detection needed** - explicit naming is more reliable than file size heuristics

### DVD-ROM Only (No Authoring)
This implementation:
- ✅ Supports reading DVD-ROM discs
- ✅ Supports reading DVD-R/DVD+R pressed discs  
- ❌ Does NOT support DVD writing/burning
- ❌ Does NOT support DVD-RAM
- ❌ Does NOT support multi-layer DVDs (yet)

The capability bits correctly reflect read-only DVD-ROM support.

## Testing

### Test CD-ROM Mode
1. Load a file named `test.iso`
2. MacOS should show: "CD-ROM drive"
3. Mode Page 0x2A byte 0 = 0x38

### Test DVD Mode
1. Load a file named `movie.dvd.iso`  
2. MacOS should show: "DVD-ROM drive"
3. Mode Page 0x2A byte 0 = 0x39

## Future Enhancements
- Multi-layer DVD support (check image size > 4.7GB)
- DVD+R DL support
- Blu-ray support (would need `.bluray.iso` or similar)
