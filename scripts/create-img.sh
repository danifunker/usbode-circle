#!/bin/bash

set -e  # Exit on any error

# Configuration
IMG_SIZE="200M"
IMG_NAME="boot.img"
SOURCE_DIR=""
OUTPUT_DIR="."
MOUNT_POINT="/tmp/boot_mount_$$"

# Function to show usage
usage() {
    echo "Usage: $0 -s SOURCE_DIR [-o OUTPUT_DIR] [-n IMG_NAME]"
    echo "  -s SOURCE_DIR   Directory to copy files from (required)"
    echo "  -o OUTPUT_DIR   Output directory (default: current directory)"
    echo "  -n IMG_NAME     Image name (default: boot.img)"
    echo "  -h              Show this help"
    exit 1
}

# Function to cleanup on exit
cleanup() {
    if mountpoint -q "$MOUNT_POINT" 2>/dev/null; then
        echo "Unmounting $MOUNT_POINT..."
        sudo umount "$MOUNT_POINT"
    fi
    if [ -d "$MOUNT_POINT" ]; then
        rmdir "$MOUNT_POINT"
    fi
}

# Set trap for cleanup
trap cleanup EXIT

# Parse command line options
while getopts "s:o:n:h" opt; do
    case $opt in
        s) SOURCE_DIR="$OPTARG" ;;
        o) OUTPUT_DIR="$OPTARG" ;;
        n) IMG_NAME="$OPTARG" ;;
        h) usage ;;
        *) usage ;;
    esac
done

# Check if source directory is provided
if [ -z "$SOURCE_DIR" ]; then
    echo "Error: Source directory is required"
    usage
fi

# Check if source directory exists
if [ ! -d "$SOURCE_DIR" ]; then
    echo "Error: Source directory '$SOURCE_DIR' does not exist"
    exit 1
fi

# Create output directory if it doesn't exist
mkdir -p "$OUTPUT_DIR"

IMG_PATH="$OUTPUT_DIR/$IMG_NAME"
COMPRESSED_PATH="$IMG_PATH.xz"

echo "Creating $IMG_SIZE disk image..."
dd if=/dev/zero of="$IMG_PATH" bs=1M count=200 status=progress

echo "Creating FAT32 filesystem..."
mkfs.fat -F 32 -n "BOOT" "$IMG_PATH"

echo "Creating mount point..."
mkdir -p "$MOUNT_POINT"

echo "Mounting image..."
sudo mount -o loop "$IMG_PATH" "$MOUNT_POINT"

echo "Copying files from $SOURCE_DIR to boot partition..."
sudo cp -r "$SOURCE_DIR"/* "$MOUNT_POINT"/

echo "Syncing filesystem..."
sync

echo "Unmounting image..."
sudo umount "$MOUNT_POINT"
rmdir "$MOUNT_POINT"

echo "Compressing image with xz..."
xz -9 -v "$IMG_PATH"

echo "Done! Created compressed image: $COMPRESSED_PATH"
echo "Original size: $(du -h "$SOURCE_DIR" | cut -f1)"
echo "Compressed size: $(du -h "$COMPRESSED_PATH" | cut -f1)"