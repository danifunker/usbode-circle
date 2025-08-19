#!/bin/bash

set -e  # Exit on any error

# Configuration
IMG_SIZE="200M"
IMG_NAME="boot.img"
SOURCE_DIR=""
OUTPUT_DIR="."
MOUNT_POINT="${HOME}/boot_mount_$$"

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
    if [ -n "$LOOP_DEVICE" ] && [ -e "$LOOP_DEVICE" ]; then
        echo "Detaching loop device $LOOP_DEVICE..."
        sudo losetup -d "$LOOP_DEVICE"
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

echo "Creating partition table and FAT32 partition..."
# Use fdisk to create a proper MBR partition table
(
echo o      # Create new DOS partition table
echo n      # New partition
echo p      # Primary partition
echo 1      # Partition number 1
echo        # Default first sector
echo        # Default last sector (use all space)
echo t      # Change partition type
echo c      # FAT32 LBA type (0x0c)
echo a      # Make partition bootable
echo w      # Write changes
) | fdisk "$IMG_PATH"

echo "Setting up loop device for partition..."
LOOP_DEVICE=$(sudo losetup --find --show --partscan "$IMG_PATH")

echo "Creating FAT32 filesystem on partition..."
sudo mkfs.fat -F 32 -n "bootfs" "${LOOP_DEVICE}p1"

echo "Creating mount point..."
mkdir -p "$MOUNT_POINT"

echo "Mounting partition..."
if ! sudo mount -o uid="$(id -u)",gid="$(id -g)",umask=000 "${LOOP_DEVICE}p1" "$MOUNT_POINT"; then
    echo "Error: Failed to mount partition"
    sudo losetup -d "$LOOP_DEVICE"
    exit 1
fi

echo "Copying files from $SOURCE_DIR to boot partition..."
if ! cp -r "$SOURCE_DIR"/* "$MOUNT_POINT"/; then
    echo "Error: Failed to copy files"
    exit 1
fi

echo "Syncing filesystem..."
sync

echo "Unmounting partition..."
if ! sudo umount "$MOUNT_POINT"; then
    echo "Error: Failed to unmount partition"
    exit 1
fi

rmdir "$MOUNT_POINT"

echo "Compressing image with xz..."
xz -v "$IMG_PATH"

echo "Done! Created compressed image: $COMPRESSED_PATH"
echo "Original size: $(du -h "$SOURCE_DIR" | cut -f1)"
echo "Compressed size: $(du -h "$COMPRESSED_PATH" | cut -f1)"