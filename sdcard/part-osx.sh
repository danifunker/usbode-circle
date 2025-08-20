#!/bin/bash
# filepath: /Users/dani/repos/usbode-circle/sdcard/part-osx.sh

# Configuration
SCRIPT_NAME="$(basename "$0")"
EXFAT_LABEL="IMGSTORE"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Print colored output
print_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
print_success() { echo -e "${GREEN}[SUCCESS]${NC} $1"; }
print_warning() { echo -e "${YELLOW}[WARNING]${NC} $1"; }
print_error() { echo -e "${RED}[ERROR]${NC} $1"; }

# Usage
usage() {
    cat << EOF
Usage: $SCRIPT_NAME [device]

Add an exFAT partition to an SD card with USBODE image.

Examples:
  $SCRIPT_NAME /dev/disk4
  $SCRIPT_NAME disk4
  $SCRIPT_NAME                    # List available devices

EOF
    exit 1
}

# Simple device validation
validate_device() {
    local device="$1"
    
    # Allow both /dev/diskN and diskN formats
    if [[ "$device" =~ ^disk[0-9]+$ ]]; then
        device="/dev/$device"
    fi
    
    if [[ ! -e "$device" ]]; then
        print_error "Device does not exist: $device"
        exit 1
    fi
    
    echo "$device"
}

# Check if device has USBODE files
check_usbode_device() {
    local device="$1"
    
    print_info "Checking device: $device"
    
    # Check for bootfs partition
    local partition_info=$(diskutil list "$device" 2>/dev/null)
    if ! echo "$partition_info" | grep -q "bootfs"; then
        print_error "No 'bootfs' partition found"
        return 1
    fi
    
    print_success "Found bootfs partition"
    
    # Find where bootfs is mounted (if anywhere)
    local bootfs_partition=$(echo "$partition_info" | grep "bootfs" | awk '{print $NF}')
    local mount_point=$(mount | grep "/dev/$bootfs_partition" | awk '{print $3}')
    
    if [[ -z "$mount_point" ]]; then
        print_info "bootfs partition not mounted, trying to mount it..."
        if ! diskutil mount "/dev/$bootfs_partition" >/dev/null 2>&1; then
            print_error "Could not mount bootfs partition"
            return 1
        fi
        mount_point=$(mount | grep "/dev/$bootfs_partition" | awk '{print $3}')
    fi
    
    if [[ -z "$mount_point" ]]; then
        print_error "Could not find mount point for bootfs"
        return 1
    fi
    
    print_info "Checking files at: $mount_point"
    
    # Simple file checks
    local has_config=false
    local has_test=false
    
    if [[ -f "$mount_point/config.txt" ]]; then
        print_success "Found config.txt"
        has_config=true
    fi
    
    if [[ -f "$mount_point/system/test.pcm" ]]; then
        print_success "Found system/test.pcm"
        has_test=true
    fi
    
    if [[ "$has_config" == true ]] && [[ "$has_test" == true ]]; then
        print_success "USBODE device verified"
        return 0
    else
        print_warning "Missing USBODE files"
        return 1
    fi
}

# List potential devices
list_devices() {
    print_info "Scanning for devices..."
    echo
    
    diskutil list | grep "physical" | while read -r line; do
        if [[ "$line" =~ ^(/dev/disk[0-9]+) ]]; then
            local disk="${BASH_REMATCH[1]}"
            local info=$(diskutil info "$disk" 2>/dev/null)
            local size=$(echo "$info" | grep "Disk Size" | sed 's/.*: //')
            local removable="No"
            
            if echo "$info" | grep -q "Removable Media.*Removable"; then
                removable="Yes"
            fi
            
            printf "%-12s %-20s Removable: %s\n" "$disk" "$size" "$removable"
            
            # Check if it has bootfs
            if diskutil list "$disk" | grep -q "bootfs"; then
                if check_usbode_device "$disk" >/dev/null 2>&1; then
                    print_success "  → This looks like a USBODE device!"
                fi
            fi
            echo
        fi
    done
}

# Main partitioning function
# Replace the partition_device function with this improved version:

# Replace the partition_device function with this macOS-compatible version:

partition_device() {
    local device="$1"
    
    print_warning "This will add a second partition to $device"
    print_warning "The boot partition will be preserved"
    echo
    read -p "Continue? (y/N): " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        print_info "Aborted"
        exit 0
    fi
    
    # Check root
    if [[ $EUID -ne 0 ]]; then
        print_error "This requires sudo privileges"
        exit 1
    fi
    
    # Show current layout
    print_info "Current partition layout:"
    diskutil list "$device"
    echo
    
    # Check if partition 2 already exists
    if diskutil list "$device" | grep -q "${device}s2"; then
        print_warning "Partition 2 already exists!"
        diskutil list "$device" | grep "${device}s2"
        read -p "Delete and recreate? (y/N): " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            print_info "Aborted"
            exit 0
        fi
    fi
    
    # Backup
    local backup_file="partition_backup_$(date +%Y%m%d_%H%M%S).img"
    print_info "Creating backup: $backup_file"
    dd if="$device" of="$backup_file" bs=1M count=1 status=none
    print_success "Backup created: $backup_file"
    
    # Unmount
    print_info "Unmounting device..."
    diskutil unmountDisk "$device" || true
    sleep 1
    
    # Use macOS fdisk with correct syntax
    print_info "Adding second partition using macOS fdisk..."
    
    # From your debug: partition 1 ends at sector 409599 (start 2048 + size 407552 - 1)
    # So partition 2 starts at 409600
    # Total sectors: 31116288, so partition 2 size = 31116288 - 409600 = 30706688
    
    local p2_start=409600
    local p2_size=30706688
    
    print_info "Partition 2 will span sectors $p2_start to $((p2_start + p2_size - 1))"
    
    # Use fdisk -e (edit mode) with proper macOS syntax
    fdisk -e "$device" <<EOF
p
e 2
07
n
$p2_start
$p2_size
f 1
w
y
quit
EOF
    
    if [[ $? -eq 0 ]]; then
        print_success "fdisk completed successfully"
    else
        print_error "fdisk failed, trying alternative method..."
        
        # Alternative: Use printf to create fdisk commands
        print_info "Trying alternative fdisk method..."
        (
            echo "e 2"      # Edit partition 2
            echo "07"       # Set type to exFAT (07)
            echo "n"        # Set new values
            echo "$p2_start"    # Start sector
            echo "$p2_size"     # Size in sectors
            echo "f 1"      # Set partition 1 as active
            echo "w"        # Write changes
            echo "y"        # Confirm
            echo "quit"     # Exit
        ) | fdisk -e "$device"
        
        if [[ $? -ne 0 ]]; then
            print_error "Both fdisk methods failed"
            exit 1
        fi
    fi
    
    # Force disk rescan
    print_info "Forcing disk rescan..."
    diskutil list "$device" > /dev/null
    
    # Wait for the system to recognize the new partition
    print_info "Waiting for partition to appear..."
    local wait_count=0
    local partition2="${device}s2"
    
    while [[ ! -e "$partition2" ]] && [[ $wait_count -lt 10 ]]; do
        sleep 1
        wait_count=$((wait_count + 1))
        print_info "Waiting... ($wait_count/10)"
        diskutil list "$device" > /dev/null  # Force rescan
    done
    
    if [[ ! -e "$partition2" ]]; then
        print_error "Partition $partition2 was not created after 10 seconds"
        print_info "Current partition table:"
        diskutil list "$device"
        fdisk "$device"
        exit 1
    fi
    
    print_success "Partition $partition2 created successfully"
    
    # Format second partition
    print_info "Formatting $partition2 as exFAT with label '$EXFAT_LABEL'..."
    
    if newfs_exfat -v "$EXFAT_LABEL" "$partition2"; then
        print_success "Formatting completed successfully"
    else
        print_error "Failed to format partition"
        exit 1
    fi
    
    # Show final result
    print_success "Done! Final layout:"
    diskutil list "$device"
    
    # Try to mount both partitions
    print_info "Mounting partitions..."
    diskutil mount "${device}s1" || print_warning "Could not mount boot partition"
    diskutil mount "${device}s2" || print_warning "Could not mount data partition"
    
    echo
    print_success "Partitioning completed successfully!"
    print_info "Boot partition: ${device}s1 (bootfs)"
    print_info "Data partition: ${device}s2 ($EXFAT_LABEL)"
}

# Add this function before main():

# Debug partition table
debug_partition_table() {
    local device="$1"
    
    print_info "=== DEBUG INFORMATION FOR $device ==="
    
    print_info "diskutil list output:"
    diskutil list "$device"
    echo
    
    print_info "fdisk output:"
    fdisk "$device"
    echo
    
    print_info "Raw partition table (hexdump):"
    dd if="$device" bs=1 skip=446 count=64 2>/dev/null | hexdump -C
    echo
    
    print_info "Device info:"
    diskutil info "$device"
    echo
}

# Update main function to add debug option:

# Main function
main() {
    if [[ $# -eq 0 ]]; then
        list_devices
        exit 0
    elif [[ $# -eq 1 ]]; then
        local device=$(validate_device "$1")
        
        if check_usbode_device "$device"; then
            partition_device "$device"
        else
            print_error "Device does not appear to be a valid USBODE device"
            exit 1
        fi
    elif [[ $# -eq 2 ]] && [[ "$2" == "debug" ]]; then
        local device=$(validate_device "$1")
        debug_partition_table "$device"
        exit 0
    else
        usage
    fi
}

main "$@"