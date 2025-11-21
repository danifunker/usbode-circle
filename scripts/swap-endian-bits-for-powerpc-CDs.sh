#!/bin/bash
# fix-cue-audio-endian.sh
# Automatically detects and fixes byte order issues in CUE/BIN audio tracks

set -e

usage() {
    echo "Usage: $0 <cue-file>"
    echo ""
    echo "Detects byte order of audio tracks in CUE/BIN and fixes if needed."
    echo "Converts files IN-PLACE (overwrites originals)."
    exit 1
}

if [ $# -ne 1 ]; then
    usage
fi

CUE_FILE="$1"

if [ ! -f "$CUE_FILE" ]; then
    echo "Error: CUE file '$CUE_FILE' not found"
    exit 1
fi

# Check dependencies
if ! command -v sox &> /dev/null; then
    echo "Error: sox is not installed. Install with: brew install sox"
    exit 1
fi

if ! command -v play &> /dev/null; then
    echo "Error: play (from sox) is not installed. Install with: brew install sox"
    exit 1
fi

echo "=== Audio Endianness Detector and Fixer ==="
echo "CUE file: $CUE_FILE"
echo ""

# Parse CUE file to find audio tracks
AUDIO_TRACKS=()
CURRENT_TRACK=""
CURRENT_FILE=""

while IFS= read -r line; do
    # Match FILE lines
    if [[ $line =~ ^[[:space:]]*FILE[[:space:]]+\"([^\"]+)\"[[:space:]]+BINARY ]]; then
        CURRENT_FILE="${BASH_REMATCH[1]}"
    fi
    
    # Match TRACK lines
    if [[ $line =~ ^[[:space:]]*TRACK[[:space:]]+([0-9]+)[[:space:]]+AUDIO ]]; then
        CURRENT_TRACK="${BASH_REMATCH[1]}"
        AUDIO_TRACKS+=("$CURRENT_TRACK:$CURRENT_FILE")
    fi
done < "$CUE_FILE"

if [ ${#AUDIO_TRACKS[@]} -eq 0 ]; then
    echo "No AUDIO tracks found in CUE file."
    exit 0
fi

echo "Found ${#AUDIO_TRACKS[@]} audio track(s)"
echo ""

# Function to detect endianness by analyzing audio data
detect_endianness() {
    local binfile="$1"
    local skip_bytes="${2:-0}"
    
    if [ ! -f "$binfile" ]; then
        echo "UNKNOWN"
        return
    fi
    
    # Extract a sample from the file (skip pregap if specified)
    # Get 10 seconds of audio = 441000 samples * 4 bytes = 1,764,000 bytes
    local sample_size=1764000
    local temp_sample=$(mktemp)
    
    dd if="$binfile" of="$temp_sample" bs=1 skip=$skip_bytes count=$sample_size 2>/dev/null
    
    # Check if sample is mostly zeros (pregap/silence)
    local zero_count=$(hexdump -v -e '1/1 "%02x\n"' "$temp_sample" | grep -c "^00$" || true)
    local total_bytes=$(stat -f%z "$temp_sample" 2>/dev/null || stat -c%s "$temp_sample")
    local zero_ratio=$((zero_count * 100 / total_bytes))
    
    if [ $zero_ratio -gt 90 ]; then
        # Mostly silence, try sampling further in
        rm "$temp_sample"
        if [ $skip_bytes -lt 1000000 ]; then
            # Try 500KB further in
            detect_endianness "$binfile" $((skip_bytes + 500000))
            return
        fi
    fi
    
    # Test both interpretations and compare maximum delta
    # Byte-swapped audio produces impossibly high deltas (sharp transitions)
    # Real audio has smooth transitions with lower maximum delta
    
    # Test little-endian interpretation
    local le_stats=$(sox -t raw -r 44100 -e signed -b 16 -c 2 --endian little "$temp_sample" \
        -n stat 2>&1)
    local le_max_delta=$(echo "$le_stats" | grep "Maximum delta:" | awk '{print $NF}')
    local le_rms=$(echo "$le_stats" | grep "RMS.*amplitude:" | awk '{print $NF}')
    
    # Test big-endian interpretation  
    local be_stats=$(sox -t raw -r 44100 -e signed -b 16 -c 2 --endian big "$temp_sample" \
        -n stat 2>&1)
    local be_max_delta=$(echo "$be_stats" | grep "Maximum delta:" | awk '{print $NF}')
    local be_rms=$(echo "$be_stats" | grep "RMS.*amplitude:" | awk '{print $NF}')
    
    rm "$temp_sample"
    
    # Decision logic:
    # 1. If max delta > 1.5, that interpretation is definitely wrong (byte-swapped)
    # 2. Otherwise, lower max delta = correct interpretation (smoother audio)
    # 3. As a tiebreaker, lower RMS also suggests correct interpretation
    
    local le_score=0
    local be_score=0
    
    # Check for impossibly high deltas (> 1.5)
    if (( $(echo "$le_max_delta > 1.5" | bc -l) )); then
        be_score=$((be_score + 100))  # Little-endian is definitely wrong
    fi
    if (( $(echo "$be_max_delta > 1.5" | bc -l) )); then
        le_score=$((le_score + 100))  # Big-endian is definitely wrong
    fi
    
    # Lower max delta is better (more realistic audio transitions)
    if (( $(echo "$le_max_delta < $be_max_delta" | bc -l) )); then
        le_score=$((le_score + 10))
    else
        be_score=$((be_score + 10))
    fi
    
    # Lower RMS as tiebreaker (byte-swapped audio often has higher RMS)
    if (( $(echo "$le_rms < $be_rms" | bc -l) )); then
        le_score=$((le_score + 1))
    else
        be_score=$((be_score + 1))
    fi
    
    # Return which format the file is CURRENTLY stored in
    if [ $le_score -gt $be_score ]; then
        echo "LITTLE"  # File is stored as little-endian
    else
        echo "BIG"     # File is stored as big-endian
    fi
}

# Check each audio track
NEEDS_CONVERSION=false
SKIP_BYTES=176400  # Skip ~1 second pregap by default

for track_info in "${AUDIO_TRACKS[@]}"; do
    TRACK_NUM="${track_info%%:*}"
    BIN_FILE="${track_info#*:}"
    
    if [ ! -f "$BIN_FILE" ]; then
        echo "Warning: $BIN_FILE not found, skipping..."
        continue
    fi
    
    echo -n "Analyzing Track $TRACK_NUM ($BIN_FILE)... "
    
    ENDIAN=$(detect_endianness "$BIN_FILE" $SKIP_BYTES)
    
    echo "detected as $ENDIAN-endian"
    
    # CD audio should be BIG-endian, so if we detect LITTLE, it needs conversion
    if [ "$ENDIAN" = "LITTLE" ]; then
        NEEDS_CONVERSION=true
    fi
done

echo ""

if [ "$NEEDS_CONVERSION" = false ]; then
    echo "✓ All audio tracks are already in correct format (big-endian)"
    echo "No conversion needed - you can burn this CUE file as-is"
    exit 0
fi

echo "⚠ Audio tracks are in little-endian format (PC byte order)"
echo "  CD audio requires big-endian format for burning"
echo ""

# Find first audio track for testing
FIRST_TRACK=""
FIRST_FILE=""
for track_info in "${AUDIO_TRACKS[@]}"; do
    TRACK_NUM="${track_info%%:*}"
    BIN_FILE="${track_info#*:}"
    if [ -f "$BIN_FILE" ]; then
        ENDIAN=$(detect_endianness "$BIN_FILE" $SKIP_BYTES)
        if [ "$ENDIAN" = "LITTLE" ]; then
            FIRST_TRACK="$TRACK_NUM"
            FIRST_FILE="$BIN_FILE"
            break
        fi
    fi
done

if [ -n "$FIRST_FILE" ]; then
    echo "Let's test the audio from Track $FIRST_TRACK to confirm it sounds correct..."
    echo "Playing 10 seconds of audio (press Ctrl+C to stop early)..."
    echo ""
    
    # Play a sample of the audio
    play -t raw -r 44100 -e signed -b 16 -c 2 --endian little "$FIRST_FILE" trim 0 10 2>/dev/null || true
    
    echo ""
    read -p "Did that sound like proper music? (y/n) " -n 1 -r
    echo ""
    
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo "Audio doesn't sound right. Conversion cancelled."
        echo "The tracks may already be in the correct format, or there may be another issue."
        exit 0
    fi
fi

echo ""
echo "WARNING: This will OVERWRITE your original audio files!"
echo "   Make sure you have a backup before proceeding."
echo ""
read -p "Convert audio tracks to big-endian IN-PLACE? (y/n) " -n 1 -r
echo ""

if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo "Conversion cancelled"
    exit 0
fi

echo ""
echo "Converting audio tracks in-place..."
echo ""

# Convert tracks
CONVERTED_COUNT=0
for track_info in "${AUDIO_TRACKS[@]}"; do
    TRACK_NUM="${track_info%%:*}"
    BIN_FILE="${track_info#*:}"
    
    if [ ! -f "$BIN_FILE" ]; then
        continue
    fi
    
    # Check if this track needs conversion
    ENDIAN=$(detect_endianness "$BIN_FILE" $SKIP_BYTES)
    
    if [ "$ENDIAN" != "LITTLE" ]; then
        echo "Track $TRACK_NUM: Already in big-endian format, skipping"
        continue
    fi
    
    TEMPFILE=$(mktemp)
    
    echo "Track $TRACK_NUM: Converting little-endian → big-endian..."
    sox -t raw -r 44100 -e signed -b 16 -c 2 --endian little "$BIN_FILE" \
        -t raw -r 44100 -e signed -b 16 -c 2 --endian big "$TEMPFILE"
    
    # Overwrite original file
    mv "$TEMPFILE" "$BIN_FILE"
    
    CONVERTED_COUNT=$((CONVERTED_COUNT + 1))
done

echo ""
echo "=== Conversion Complete! ==="
echo "Converted $CONVERTED_COUNT audio track(s)"
echo "Files have been converted in-place (originals overwritten)"
echo ""

# Test the converted audio
if [ -n "$FIRST_FILE" ] && [ -f "$FIRST_FILE" ]; then
    echo "Testing converted audio from Track $FIRST_TRACK..."
    echo "Playing 10 seconds (press Ctrl+C to stop)..."
    echo ""
    
    play -t raw -r 44100 -e signed -b 16 -c 2 --endian big "$FIRST_FILE" trim 0 10 2>/dev/null || true
    
    echo ""
    read -p "Does the converted audio sound correct? (y/n) " -n 1 -r
    echo ""
    
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo ""
        echo "Audio doesn't sound right after conversion!"
        echo "You may need to restore from backup and investigate further."
    else
        echo ""
        echo "✓ Conversion successful!"
        echo "Your CUE file is ready to burn with:"
        echo "  cdrdao write --device /dev/rdiskX --speed 8 \"$CUE_FILE\""
    fi
fi

echo ""