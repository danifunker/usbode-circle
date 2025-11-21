cat > ccd2chd.sh << 'EOF'
#!/bin/bash

# CCD to CHD converter with subchannel preservation
# Usage: ./ccd2chd.sh input.ccd [--no-subchan]

set -e

if [ $# -lt 1 ]; then
    echo "Usage: $0 input.ccd [--no-subchan]"
    echo ""
    echo "Converts CloneCD (CCD/IMG/SUB) to CHD format with subchannel preservation"
    echo ""
    echo "Options:"
    echo "  --no-subchan    Don't add .subchan. to output filename (subchannels still preserved)"
    echo ""
    echo "Examples:"
    echo "  $0 game.ccd                 # Creates game.subchan.chd"
    echo "  $0 game.ccd --no-subchan    # Creates game.chd"
    exit 1
fi

CCD_FILE="$1"
ADD_SUBCHAN_FLAG=true

if [ "$2" == "--no-subchan" ]; then
    ADD_SUBCHAN_FLAG=false
fi

# Check if file exists
if [ ! -f "$CCD_FILE" ]; then
    echo "Error: File '$CCD_FILE' not found"
    exit 1
fi

# Get the base filename without extension
BASENAME="${CCD_FILE%.ccd}"
BASENAME="${BASENAME%.CCD}"

# Derive the IMG and SUB filenames
IMG_FILE="${BASENAME}.img"
SUB_FILE="${BASENAME}.sub"

# Check for uppercase extensions
if [ ! -f "$IMG_FILE" ]; then
    IMG_FILE="${BASENAME}.IMG"
fi

if [ ! -f "$SUB_FILE" ]; then
    SUB_FILE="${BASENAME}.SUB"
fi

# Verify all required files exist
if [ ! -f "$IMG_FILE" ]; then
    echo "Error: Image file not found. Expected: ${BASENAME}.img or ${BASENAME}.IMG"
    exit 1
fi

if [ ! -f "$SUB_FILE" ]; then
    echo "Error: Subchannel file not found. Expected: ${BASENAME}.sub or ${BASENAME}.SUB"
    exit 1
fi

# Create output filename
if [ "$ADD_SUBCHAN_FLAG" = true ]; then
    OUTPUT_CHD="${BASENAME}.subchan.chd"
else
    OUTPUT_CHD="${BASENAME}.chd"
fi

TEMP_BIN="${BASENAME}.merged.bin"
TEMP_CUE="${BASENAME}.merged.cue"

echo "=================================================="
echo "CCD to CHD Converter"
echo "=================================================="
echo "Input CCD:    $CCD_FILE"
echo "Input IMG:    $IMG_FILE"
echo "Input SUB:    $SUB_FILE"
echo "Output CHD:   $OUTPUT_CHD"
echo "=================================================="
echo ""

# Step 1: Merge IMG and SUB into a single BIN with subchannels
echo "[1/4] Merging IMG and SUB files..."

python3 - "$IMG_FILE" "$SUB_FILE" "$TEMP_BIN" << 'PYTHON'
import sys

img_file = sys.argv[1]
sub_file = sys.argv[2]
out_file = sys.argv[3]

with open(img_file, 'rb') as img, open(sub_file, 'rb') as sub, open(out_file, 'wb') as out:
    sector_count = 0
    while True:
        sector = img.read(2352)
        subchan = sub.read(96)
        
        if not sector:
            break
            
        out.write(sector)
        if subchan:
            out.write(subchan)
        else:
            out.write(b'\x00' * 96)
        
        sector_count += 1
    
    print(f"  Merged {sector_count} sectors with subchannel data")
PYTHON

# Step 2: Create CUE file
echo "[2/4] Creating CUE sheet..."

cat > "$TEMP_CUE" << CUEEOF
FILE "$TEMP_BIN" BINARY
  TRACK 01 MODE1/2352
    INDEX 01 00:00:00
CUEEOF

echo "  Created $TEMP_CUE"

# Step 3: Convert to CHD
echo "[3/4] Converting to CHD format..."

if [ -f "$OUTPUT_CHD" ]; then
    echo "  Warning: $OUTPUT_CHD already exists, overwriting..."
    rm "$OUTPUT_CHD"
fi

chdman createcd -i "$TEMP_CUE" -o "$OUTPUT_CHD"

# Step 4: Verify and cleanup
echo "[4/4] Verifying and cleaning up..."

UNIT_SIZE=$(chdman info -i "$OUTPUT_CHD" | grep "Unit Size" | awk '{print $3}')

echo ""
echo "=================================================="
echo "Conversion complete!"
echo "=================================================="
echo "Output file:  $OUTPUT_CHD"
echo "Unit size:    $UNIT_SIZE bytes"

if [ "$UNIT_SIZE" == "2,448" ]; then
    echo "Status:       ✓ Subchannels preserved"
else
    echo "Status:       ⚠ Warning: Expected 2,448 bytes"
fi

echo ""
echo "Cleaning up temporary files..."
rm -f "$TEMP_BIN" "$TEMP_CUE"

echo ""
echo "Done! Copy $OUTPUT_CHD to your USBODE SD card."
if [ "$ADD_SUBCHAN_FLAG" = true ]; then
    echo "The .subchan. flag will enable subchannel support in USBODE."
fi
echo "=================================================="
EOF

chmod +x ccd2chd.sh
