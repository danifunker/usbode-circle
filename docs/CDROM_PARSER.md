# CUE Parser Analysis and Comparison

## Current Implementation

### Location
- **Header:** `addon/cueparser/cueparser.h`
- **Implementation:** `addon/cueparser/cueparser.cpp`

### Public API

```cpp
class CUEParser {
public:
    CUEParser();
    CUEParser(const char *cue_sheet);
    void restart();
    const CUETrackInfo *next_track();
    const CUETrackInfo *next_track(uint64_t prev_file_size);
};
```

### Data Structure

```cpp
struct CUETrackInfo {
    char filename[CUE_MAX_FILENAME + 1];  // Source .bin file name
    int file_index;                        // Index of which .bin file (0-based)
    CUEFileMode file_mode;                 // BINARY, MOTOROLA, MP3, WAVE, AIFF
    uint64_t file_offset;                  // Byte offset into the .bin file

    int track_number;                      // CD track number (1-99)
    CUETrackMode track_mode;               // AUDIO, MODE1/2048, MODE1/2352, etc.
    uint32_t sector_length;                // Sector size in bytes

    uint32_t unstored_pregap_length;       // PREGAP: Silence not in .bin file
    uint32_t cumulative_offset;            // Running total of unstored pregap

    uint32_t file_start;                   // LBA start of file
    uint32_t data_start;                   // LBA start of track data (INDEX 01)
    uint32_t track_start;                  // LBA start including pregap (INDEX 00)
};
```

### Supported Track Modes

| Mode | Sector Size | Description |
|------|-------------|-------------|
| AUDIO | 2352 | Audio track |
| CDG | 2448 | CD+G with subchannel |
| MODE1/2048 | 2048 | Data track, cooked |
| MODE1/2352 | 2352 | Data track, raw with headers |
| MODE2/2336 | 2336 | Mode 2 formless |
| MODE2/2352 | 2352 | Mode 2 raw |

### Files That Depend on CUEParser

| File | Usage |
|------|-------|
| `addon/usbcdgadget/usbcdgadget.h:34,536` | Includes header, holds `CUEParser cueParser` member |
| `addon/usbcdgadget/cd_utils.cpp:106-331` | Heavy usage - `restart()`, `next_track()` for track lookups |
| `addon/usbcdgadget/scsi_toc.cpp:118-138` | Uses `track_mode`, `data_start`, `track_number` |
| `addon/discimage/cuebinfile.cpp` | Implements `GetCueSheet()`, stubbed `ParseCueSheet()` |

### Usage Patterns in Codebase

**Pattern 1: Get First Track**
```cpp
gadget->cueParser.restart();
const CUETrackInfo *trackInfo = gadget->cueParser.next_track();
```

**Pattern 2: Iterate All Tracks**
```cpp
gadget->cueParser.restart();
while ((trackInfo = gadget->cueParser.next_track()) != nullptr) {
    // Process trackInfo
}
```

**Pattern 3: Find Track by LBA**
```cpp
gadget->cueParser.restart();
while ((trackInfo = gadget->cueParser.next_track()) != nullptr) {
    if (trackInfo->track_start == lba) return *trackInfo;
    if (lba < trackInfo->track_start) return lastTrack;
    lastTrack = *trackInfo;
}
```

### Current Limitations

1. **Multi-bin support incomplete:** Parser has infrastructure (`file_index`, `prev_file_size` parameter) but consumer code never uses it properly
2. **No random access:** Must iterate from beginning to find a track
3. **No track caching:** Re-parses on every query
4. **No CD-TEXT support**
5. **No REM comments support**
6. **No track flags** (pre-emphasis, copy permitted, etc.)
7. **No ISRC codes**
8. **Limited index support:** Only INDEX 00 and INDEX 01

### Multi-Bin Status

The README explicitly states:
> "BIN/CUE images with multiple .BIN files do not yet work correctly."

The parser itself tracks `file_index` and accepts `prev_file_size`, but:
- `cd_utils.cpp` always calls `next_track()` without file size
- `CCueBinFileDevice` only holds one file handle (`FIL* m_pFile`)
- No mechanism to switch between multiple .bin files during playback

---

## Comparison: Alternative CUE Parsers

### Feature Comparison Matrix

| Feature | Current | libcue | libmirage |
|---------|---------|--------|-----------|
| **Language** | C++ | C | C (GLib) |
| **License** | - | BSD/GPLv2 | GPLv2+ |
| **Parse from string** | Yes | Yes | Yes |
| **Parse from file** | No | Yes | Yes |
| **Random track access** | No | Yes | Yes |
| **Track count query** | No | Yes | Yes |
| **Multi-bin support** | Partial | Yes | Yes |
| **CD-TEXT** | No | Yes | Yes |
| **REM comments** | No | Yes | Yes |
| **Track flags** | No | Yes | Yes |
| **ISRC codes** | No | Yes | Yes |
| **Multiple indexes** | Limited | Yes | Yes |
| **Multi-session** | No | No | Yes |
| **Memory allocation** | Minimal | Heap | Heavy (GLib) |
| **Dependencies** | None | None | GLib, GObject |
| **Embedded suitable** | Yes | Adaptable | No |

---

### Option 1: libcue

**Repository:** https://github.com/lipnitsk/libcue

#### API Overview

```c
// Parsing
Cd* cue_parse_file(FILE*);
Cd* cue_parse_string(const char*);
void cd_delete(Cd* cd);

// CD-level
enum DiscMode cd_get_mode(const Cd *cd);
const char *cd_get_catalog(const Cd *cd);
int cd_get_ntrack(const Cd *cd);
Cdtext *cd_get_cdtext(const Cd *cd);

// Track-level (random access!)
Track *cd_get_track(const Cd *cd, int i);
const char *track_get_filename(const Track *track);
long track_get_start(const Track *track);
long track_get_length(const Track *track);
enum TrackMode track_get_mode(const Track *track);
int track_is_set_flag(const Track *track, enum TrackFlag flag);
long track_get_index(const Track *track, int i);
const char *track_get_isrc(const Track *track);
```

#### Track Modes

```c
enum TrackMode {
    MODE_AUDIO,         // 2352 byte block length
    MODE_MODE1,         // 2048 byte block length
    MODE_MODE1_RAW,     // 2352 byte block length
    MODE_MODE2,         // 2336 byte block length
    MODE_MODE2_FORM1,   // 2048 byte block length
    MODE_MODE2_FORM2,   // 2324 byte block length
    MODE_MODE2_FORM_MIX,// 2332 byte block length
    MODE_MODE2_RAW      // 2352 byte block length
};
```

#### Pros
- Standalone library, no heavy dependencies
- Well-maintained (~1,400 lines of code)
- Mature and battle-tested
- Full feature set (CD-TEXT, flags, ISRC, etc.)
- Random track access
- `cue_parse_string()` matches current constructor pattern

#### Cons
- Uses flex/bison parser generator (adds complexity)
- Heap allocation required (`Cd*` must be freed)
- Returns `long` for positions (need to map to `uint32_t`)
- Missing fields needed by current code:
  - `file_offset` - byte offset into bin file
  - `file_index` - which bin file (0-based)
  - `sector_length` - must derive from TrackMode
  - `prev_file_size` handling for LBA calculations

#### Adaptation Effort
**Medium** - Could wrap libcue and compute missing fields, or extract/simplify the parsing logic.

---

### Option 2: libmirage

**Repository:** https://github.com/cdemu/cdemu/tree/master/libmirage/images/image-cue

#### Architecture

```c
struct _MirageParserCuePrivate {
    MirageDisc *disc;
    gchar *cur_data_filename;      // Current binary file (multi-bin!)
    gchar *cur_data_type;          // BINARY or AUDIO
    gint cur_data_sectsize;        // Sector size
    gint binary_offset;            // Offset within current binary file
    MirageSession *cur_session;
    MirageTrack *cur_track;
    MirageTrack *prev_track;
    GList *regex_rules;            // Regex-based parsing rules
};
```

#### Parsing Approach
- Regex-based line matching with callbacks
- GObject plugin architecture
- Tracks current file and offset separately
- Resets `binary_offset` when switching files

#### Multi-Bin Handling
When `FILE` directive encountered:
1. Finishes previous track (`mirage_parser_cue_finish_last_track()`)
2. Locates new data file (`mirage_helper_find_data_file()`)
3. Stores new filename in `cur_data_filename`
4. Resets `binary_offset` to 0

#### Pros
- Very robust multi-bin support
- Handles edge cases (UltraISO, IsoBuster formats)
- Multi-session support
- CD-TEXT support (from .cdt files)
- Well-documented parsing logic

#### Cons
- **Heavy GLib dependency** - Not suitable for bare-metal
- **GObject type system** - Complex plugin architecture
- **Regex-based parsing** - Requires GLib regex engine
- **Not portable** - Tightly coupled to GLib/GObject

#### Adaptation Effort
**High** - Would need to extract parsing logic and rewrite without GLib dependencies. Useful as a reference but not directly usable.

---

## Recommendations

### For Minimal Code Changes

To preserve the existing `CUEParser` API and `CUETrackInfo` struct:

1. **Write a new parser** that maintains the same interface
2. **Borrow parsing logic** from libcue (simpler) or libmirage (more robust)
3. **Add proper multi-bin tracking** similar to libmirage's approach:
   - Track `cur_data_filename` and `binary_offset` per file
   - Reset offset when switching files
   - Pass actual file sizes when iterating

### Interface to Preserve

```cpp
// These must remain unchanged for minimal code changes
CUEParser(const char *cue_sheet);
void restart();
const CUETrackInfo *next_track();
const CUETrackInfo *next_track(uint64_t prev_file_size);

// CUETrackInfo fields used by consumer code:
// - filename, file_index, file_offset
// - track_number, track_mode, sector_length
// - file_start, data_start, track_start
// - unstored_pregap_length, cumulative_offset
```

### Optional Enhancements

New fields could be added to `CUETrackInfo` without breaking existing code:
- `isrc[13]` - ISRC code
- `flags` - Track flags bitmask
- `index[100]` - All index points
- `title`, `performer` - CD-TEXT fields

---

## New Implementation (January 2025)

A complete rewrite of the CUE parser was implemented, inspired by 86Box's CD-ROM image handling.
The new parser is licensed under GPL-3.0-or-later and provides full feature parity with libcue
while remaining suitable for embedded systems.

### Source

- **Reference:** [86Box cdrom_image.c](https://github.com/86Box/86Box/blob/master/src/cdrom/cdrom_image.c) (GPL-2.0-or-later)
- **License:** GPL-3.0-or-later (compatible with 86Box's GPL-2.0-or-later)

### Architecture Changes

| Aspect | Old Parser | New Parser |
|--------|------------|------------|
| **Parsing model** | Streaming (parse on-demand) | Upfront (parse all tracks into array) |
| **Track storage** | Single track buffer | `m_tracks[99]` array |
| **File tracking** | Implicit via `file_index` | Explicit `CUEFileEntry` array |
| **Token parsing** | Inline `strncasecmp` | Dedicated `extract_token()` |
| **LBA calculation** | On-the-fly | Separate `recalculate_lba_positions()` pass |
| **Random access** | No | Yes - `get_track(int)` |

### Complete Feature Support

| Feature | Directive | Support |
|---------|-----------|---------|
| File declaration | `FILE "name" BINARY` | ✓ Multi-bin |
| Track declaration | `TRACK 01 MODE1/2352` | ✓ All modes |
| Index points | `INDEX 00/01/02...` | ✓ Up to 100 |
| Pregap | `PREGAP 00:02:00` | ✓ |
| Postgap | `POSTGAP 00:02:00` | ✓ |
| Track flags | `FLAGS PRE DCP 4CH SCMS` | ✓ |
| ISRC code | `ISRC USRC17607839` | ✓ |
| Catalog | `CATALOG 1234567890123` | ✓ |
| CD-TEXT file | `CDTEXTFILE "disc.cdt"` | ✓ |
| Title | `TITLE "Name"` | ✓ Disc & track |
| Performer | `PERFORMER "Artist"` | ✓ Disc & track |
| Songwriter | `SONGWRITER "Writer"` | ✓ Disc & track |
| Composer | `COMPOSER "Name"` | ✓ Disc & track |
| Arranger | `ARRANGER "Name"` | ✓ Disc & track |
| Message | `MESSAGE "Text"` | ✓ Disc & track |
| REM Date | `REM DATE 1995` | ✓ |
| REM Genre | `REM GENRE "Rock"` | ✓ |
| REM DiscID | `REM DISCID 12345678` | ✓ |
| REM Comment | `REM COMMENT "text"` | ✓ |
| REM Disc Number | `REM DISCNUMBER 1` | ✓ |
| REM Total Discs | `REM TOTALDISCS 2` | ✓ |
| REM ReplayGain | `REM REPLAYGAIN_*` | ✓ Album & track |
| Multi-session | Multiple sessions | ✗ Not implemented |

### Public API

```cpp
class CUEParser {
public:
    CUEParser();
    CUEParser(const char *cue_sheet);

    // Iteration
    void restart();
    const CUETrackInfo *next_track();
    const CUETrackInfo *next_track(uint64_t prev_file_size);

    // Random access
    int get_track_count() const;
    const CUETrackInfo *get_track(int track_number);

    // Multi-bin support
    void set_file_size(int file_index, uint64_t size);

    // Disc metadata
    const CUEDiscInfo *get_disc_info() const;
    const char *get_catalog() const;
    const char *get_cdtextfile() const;
    const CUECDText *get_disc_cdtext() const;
    const CUERem *get_disc_rem() const;
};
```

### Data Structures

```cpp
// Track information (extended)
struct CUETrackInfo {
    // File info
    char filename[CUE_MAX_FILENAME + 1];
    int file_index;
    CUEFileMode file_mode;
    uint64_t file_offset;

    // Track info
    int track_number;
    CUETrackMode track_mode;
    uint32_t sector_length;
    uint8_t flags;                  // NEW: CUETrackFlags bitmask
    char isrc[CUE_ISRC_LENGTH + 1]; // NEW: ISRC code

    // Timing
    uint32_t unstored_pregap_length;
    uint32_t cumulative_offset;
    uint32_t file_start;
    uint32_t data_start;
    uint32_t track_start;

    // Metadata
    CUECDText cdtext;               // NEW: Per-track CD-TEXT
    CUERem rem;                     // NEW: Per-track REM
};

// CD-TEXT fields
struct CUECDText {
    char title[CUE_MAX_CDTEXT + 1];
    char performer[CUE_MAX_CDTEXT + 1];
    char songwriter[CUE_MAX_CDTEXT + 1];
    char composer[CUE_MAX_CDTEXT + 1];
    char arranger[CUE_MAX_CDTEXT + 1];
    char message[CUE_MAX_CDTEXT + 1];
};

// REM comment fields
struct CUERem {
    char date[CUE_MAX_REM + 1];
    char genre[CUE_MAX_REM + 1];
    char discid[CUE_MAX_REM + 1];
    char comment[CUE_MAX_REM + 1];
    int disc_number;
    int total_discs;
    char replaygain_album_gain[32];
    char replaygain_album_peak[32];
    char replaygain_track_gain[32];
    char replaygain_track_peak[32];
};

// Disc-level metadata
struct CUEDiscInfo {
    char catalog[CUE_CATALOG_LENGTH + 1];
    char cdtextfile[CUE_MAX_FILENAME + 1];
    CUECDText cdtext;
    CUERem rem;
};
```

### Track Flags

```cpp
enum CUETrackFlags {
    CUEFlag_NONE = 0x00,
    CUEFlag_PRE  = 0x01,    // Pre-emphasis
    CUEFlag_DCP  = 0x02,    // Digital copy permitted
    CUEFlag_4CH  = 0x04,    // Four channel audio
    CUEFlag_SCMS = 0x08,    // Serial copy management system
};
```

### Backward Compatibility

The new implementation maintains full backward compatibility with existing code:

- Same class name and constructor signature
- Same `restart()` and `next_track()` methods
- Same `CUETrackInfo` field names and types
- All existing usage patterns continue to work unchanged

New features are additive and do not affect existing code that doesn't use them.
