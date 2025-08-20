#!/bin/bash
# filepath: /Users/dani/repos/usbode-circle/sdcard/part-osx.sh

# Simple SD card partitioning script for USBODE
EXFAT_LABEL="IMGSTORE"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

print_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
print_success() { echo -e "${GREEN}[SUCCESS]${NC} $1"; }
print_warning() { echo -e "${YELLOW}[WARNING]${NC} $1"; }
print_error() { echo -e "${RED}[ERROR]${NC} $1"; }

# Update the usage function to include the new option:
usage() {
    echo "Usage: $0 [device|--migrate device]"
    echo
    echo "Examples:"
    echo "  $0                    # List available devices"
    echo "  $0 /dev/disk4         # Partition specific device"
    echo "  $0 disk4              # Partition specific device (short form)"
    echo "  $0 --migrate disk4    # Only migrate images (troubleshooting)"
    exit 1
}


# Check if device is a USBODE SD card
is_usbode_device() {
    local device="$1"
    
    # Check for bootfs partition
    if ! diskutil list "$device" | grep -q "bootfs"; then
        return 1
    fi
    
    # Try to mount bootfs and check for USBODE files
    local bootfs_vol="/Volumes/bootfs"
    diskutil mount "${device}s1" >/dev/null 2>&1
    
    if [[ -f "$bootfs_vol/config.txt" ]] && [[ -f "$bootfs_vol/system/test.pcm" ]]; then
        return 0
    else
        return 1
    fi
}

# List all potential USBODE devices
list_devices() {
    print_info "Scanning for USBODE devices..."
    echo
    
    local found_any=false
    
    # Check all physical disks
    for disk in /dev/disk*; do
        if [[ "$disk" =~ /dev/disk[0-9]+$ ]]; then
            local disk_info=$(diskutil info "$disk" 2>/dev/null)
            
            # Skip if not removable
            if ! echo "$disk_info" | grep -q "Removable Media.*Removable"; then
                continue
            fi
            
            local size=$(echo "$disk_info" | grep "Disk Size" | sed 's/.*: //')
            
            if is_usbode_device "$disk"; then
                printf "✓ %-12s %s (USBODE device)\n" "$disk" "$size"
                found_any=true
            else
                printf "  %-12s %s\n" "$disk" "$size"
            fi
        fi
    done
    
    if [[ "$found_any" == false ]]; then
        print_warning "No USBODE devices found"
        echo "Make sure your SD card is inserted and contains a USBODE image"
    fi
}

# Function to migrate images from boot partition to data partition
migrate_images() {
    local device="$1"
    local bootfs_mount="$2"
    local data_mount="$3"
    
    print_info "Checking for images folder to migrate..."
    
    local bootfs_images_dir="$bootfs_mount/images"
    echo "Contents of $bootfs_mount:"
    ls -la "$bootfs_mount"
    if [[ -d "$bootfs_images_dir" ]]; then
        print_info "Found images folder at: $bootfs_images_dir"
        print_info "Moving all contents to $data_mount..."
        
        # Copy all contents from images folder to data partition root
        if cp -r "$bootfs_images_dir"/* "$data_mount/" 2>/dev/null; then
            print_success "✓ Images copied successfully"
            
            # Remove the images folder from bootfs
            if rm -rf "$bootfs_images_dir"; then
                print_success "✓ Images folder removed from boot partition"
                print_success "✓ All images moved to data partition"
            else
                print_warning "Images copied but could not remove original folder"
            fi
        else
            print_error "Failed to copy images - original folder preserved"
        fi
    else
        print_info "No images folder found - nothing to migrate"
    fi
}

# Function to create and format the second partition
create_partition() {
    local device="$1"
    
    # Validate device
    if [[ ! -e "$device" ]]; then
        print_error "Device $device does not exist"
        exit 1
    fi
    
    # Check if it's a USBODE device
    if ! is_usbode_device "$device"; then
        print_error "Device $device does not appear to be a USBODE device"
        exit 1
    fi
    
    print_info "Found USBODE device: $device"
    
    # Check if already has second partition
    if diskutil list "$device" | grep -q "${device}s2"; then
        print_success "Device already has a second partition!"
        diskutil list "$device"
        return 0
    fi
    
    # Get confirmation
    print_warning "This will add a second partition to $device"
    echo "Current layout:"
    diskutil list "$device"
    echo
    read -p "Continue? (y/N): " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        print_info "Aborted"
        exit 0
    fi
    
    # Check for root
    if [[ $EUID -ne 0 ]]; then
        print_error "This requires sudo privileges"
        print_info "Please run: sudo $0 $device"
        exit 1
    fi
    
    # Unmount all partitions for this device
    print_info "Unmounting all partitions for $device..."
    diskutil unmountDisk force "$device" >/dev/null 2>&1
    sleep 2
    
    # Verify everything is unmounted
    if mount | grep -q "^$device"; then
        print_warning "Some partitions still mounted, trying one more time..."
        diskutil unmountDisk force "$device" >/dev/null 2>&1
        sleep 2
        
        if mount | grep -q "^$device"; then
            print_error "Cannot completely unmount device. Current mounts:"
            mount | grep "^$device"
            exit 1
        fi
    fi
    
    print_success "All partitions unmounted successfully"
    
    # Get partition info using fdisk
    print_info "Analyzing current partition table..."
    local fdisk_output=$(fdisk "$device")
    echo "$fdisk_output"
    
    # Extract partition 1 info using bracket contents
    local p1_line=$(echo "$fdisk_output" | grep "^\*1:")
    print_info "Partition 1 line: '$p1_line'"
    
    # Use awk to extract the bracket contents
    local bracket_nums=$(echo "$p1_line" | awk -F'[' '{print $2}' | awk -F']' '{print $1}')
    local p1_start=$(echo "$bracket_nums" | awk '{print $1}')
    local p1_size=$(echo "$bracket_nums" | awk '{print $3}')
    
    # Extract total sectors
    local total_sectors=$(echo "$fdisk_output" | grep '\[.*sectors\]' | sed 's/.*\[\([0-9]*\) sectors\].*/\1/')
    
    print_info "Parsing results:"
    print_info "  bracket_nums='$bracket_nums'"
    print_info "  p1_start='$p1_start'"
    print_info "  p1_size='$p1_size'"
    print_info "  total_sectors='$total_sectors'"
    
    # Validate parsing worked
    if [[ -z "$p1_start" ]] || [[ -z "$p1_size" ]] || [[ -z "$total_sectors" ]]; then
        print_error "Parsing failed"
        print_info "p1_start='$p1_start', p1_size='$p1_size', total_sectors='$total_sectors'"
        exit 1
    fi
    
    # Calculate partition 2 parameters
    local p2_start=$((p1_start + p1_size))
    local p2_size=$((total_sectors - p2_start))
    
    print_info "Calculated partition 2:"
    print_info "  start=$p2_start"
    print_info "  size=$p2_size"
    
    # Validate calculations
    if [[ $p2_start -le 0 ]] || [[ $p2_size -le 0 ]]; then
        print_error "Invalid partition calculations: start=$p2_start, size=$p2_size"
        exit 1
    fi
    
    # Create partition using fdisk
    print_info "Creating partition 2..."
    
    {
        echo "e 2"        # Edit partition 2
        echo "07"         # exFAT type
        echo "n"          # New values
        echo "$p2_start"  # Start sector
        echo "$p2_size"   # Size in sectors
        echo "f 1"        # Set partition 1 active
        echo "w"          # Write
        echo "quit"       # Exit
    } | fdisk -e "$device"
    
    local fdisk_result=$?
    print_info "fdisk returned: $fdisk_result"
    
    if [[ $fdisk_result -ne 0 ]]; then
        print_warning "fdisk returned error, but partition may still be created"
    fi
    
    # Force disk rescan
    print_info "Rescanning disk..."
    diskutil list "$device" >/dev/null 2>&1
    sleep 3
    
    # Check if partition was created
    local partition2="${device}s2"
    if [[ ! -e "$partition2" ]]; then
        print_error "Failed to create partition $partition2"
        print_info "Final partition table:"
        diskutil list "$device"
        exit 1
    fi
    
    print_success "Partition created successfully"
    
    # Wait for the system to settle and potentially auto-format
    print_info "Waiting for system to settle..."
    sleep 3
    
    # Check if partition is already formatted and mounted
    local is_mounted=false
    local current_mount=""
    local data_mount=""
    
    # Check if it's already mounted
    if mount | grep -q "$partition2"; then
        current_mount=$(mount | grep "$partition2" | awk '{print $3}')
        is_mounted=true
        print_success "Partition is already mounted at: $current_mount"
        data_mount="$current_mount"
    fi
    
    # Check if it's already formatted (even if not mounted)
    local partition_info=$(diskutil info "$partition2" 2>/dev/null)
    local current_format=""
    local current_label=""
    
    if [[ -n "$partition_info" ]]; then
        current_format=$(echo "$partition_info" | grep "File System Personality" | sed 's/.*: *//')
        current_label=$(echo "$partition_info" | grep "Volume Name" | sed 's/.*: *//')
        
        print_info "Partition info:"
        print_info "  Format: $current_format"
        print_info "  Label: $current_label"
        
        # Check if it's already formatted as exFAT
        if [[ "$current_format" == "ExFAT" ]]; then
            print_success "Partition is already formatted as exFAT!"
            
            # Check if it has the right label
            if [[ "$current_label" == "$EXFAT_LABEL" ]]; then
                print_success "Partition already has correct label: $EXFAT_LABEL"
            else
                print_info "Renaming partition from '$current_label' to '$EXFAT_LABEL'..."
                if diskutil rename "$partition2" "$EXFAT_LABEL"; then
                    print_success "Partition renamed successfully"
                else
                    print_warning "Could not rename partition, but it's still usable"
                fi
            fi
            
            # Mount if not already mounted
            if [[ "$is_mounted" == false ]]; then
                print_info "Mounting partition..."
                if diskutil mount "$partition2" >/dev/null 2>&1; then
                    data_mount="/Volumes/$EXFAT_LABEL"
                    print_success "Partition mounted at: $data_mount"
                else
                    print_error "Could not mount partition"
                    exit 1
                fi
            fi
        else
            print_info "Partition is formatted as $current_format, reformatting as exFAT..."
            
            # Unmount if mounted
            if [[ "$is_mounted" == true ]]; then
                diskutil unmount "$partition2" >/dev/null 2>&1
            fi
            
            # Format as exFAT
            if newfs_exfat -v "$EXFAT_LABEL" "$partition2" >/dev/null; then
                print_success "Formatting completed"
                # Mount the newly formatted partition
                diskutil mount "$partition2" >/dev/null 2>&1
                data_mount="/Volumes/$EXFAT_LABEL"
            else
                print_error "Formatting failed"
                exit 1
            fi
        fi
    else
        print_info "Could not get partition info, attempting to format..."
        
        # Format as exFAT
        if newfs_exfat -v "$EXFAT_LABEL" "$partition2" >/dev/null; then
            print_success "Formatting completed"
            # Mount the newly formatted partition
            diskutil mount "$partition2" >/dev/null 2>&1
            data_mount="/Volumes/$EXFAT_LABEL"
        else
            print_error "Formatting failed"
            exit 1
        fi
    fi
    
    # Ensure data_mount is set
    if [[ -z "$data_mount" ]]; then
        data_mount="/Volumes/$EXFAT_LABEL"
    fi
    
    # Return the data mount point for use by caller
    echo "$data_mount"
}

# Main add_partition function that orchestrates the process
add_partition() {
    local device="$1"
    
    # Create the partition and get the data mount point
    local data_mount=$(create_partition "$device")
    
    # Mount bootfs partition
    print_info "Mounting boot partition..."
    diskutil mount "${device}s1" >/dev/null 2>&1
    
    # Wait for mount to complete
    sleep 2
    
    # Find the bootfs mount point dynamically
    local bootfs_mount=""
    while IFS= read -r line; do
        if [[ "$line" =~ ${device}s1 ]]; then
            bootfs_mount=$(echo "$line" | awk '{print $3}')
            break
        fi
    done < <(mount | grep "^${device}s1")
    
    if [[ -z "$bootfs_mount" ]]; then
        print_error "Could not find mount point for boot partition"
        exit 1
    fi
    
    print_success "Partitions mounted:"
    print_info "  Boot: $bootfs_mount"
    print_info "  Data: $data_mount"
    
    # Migrate images from boot to data partition
    migrate_images "$device" "$bootfs_mount" "$data_mount"
    
    # Show final result
    print_success "Done! Final layout:"
    diskutil list "$device"
    echo
    print_success "Partitions:"
    print_info "  Boot: $bootfs_mount"
    print_info "  Data: $data_mount"
    echo
    print_info "You can copy ISO/image files directly to: $data_mount/"
}

# Main function
main() {
    if [[ $# -eq 0 ]]; then
        list_devices
    elif [[ $# -eq 1 ]]; then
        local device="$1"
        
        # Allow short form (disk4 -> /dev/disk4)
        if [[ "$device" =~ ^disk[0-9]+$ ]]; then
            device="/dev/$device"
        fi
        
        add_partition "$device"
    elif [[ $# -eq 2 ]] && [[ "$1" == "--migrate" ]]; then
        local device="$2"
        
        # Allow short form (disk4 -> /dev/disk4)
        if [[ "$device" =~ ^disk[0-9]+$ ]]; then
            device="/dev/$device"
        fi
        
        # Validate device
        if [[ ! -e "$device" ]]; then
            print_error "Device $device does not exist"
            exit 1
        fi
        
        # Check if it's a USBODE device
        if ! is_usbode_device "$device"; then
            print_error "Device $device does not appear to be a USBODE device"
            exit 1
        fi
        
        print_info "Found USBODE device: $device"
        
        # Check if it has second partition
        local disk_name=$(basename "$device")  # Gets "disk4" from "/dev/disk4"
        if ! diskutil list "$device" | grep -q "${disk_name}s2"; then
            print_error "Device does not have a second partition yet"
            print_info "Run: sudo $0 $device  (to create partition first)"
            exit 1
        fi
        
        # Mount both partitions
        print_info "Mounting partitions..."
        diskutil mount "${device}s1" >/dev/null 2>&1
        diskutil mount "${device}s2" >/dev/null 2>&1
        
        # Wait for mount to complete
        sleep 2
        
        # Find the bootfs mount point dynamically
        local bootfs_mount=""
        while IFS= read -r line; do
            if [[ "$line" =~ ${device}s1 ]]; then
                bootfs_mount=$(echo "$line" | awk '{print $3}')
                break
            fi
        done < <(mount | grep "^${device}s1")
        
        # Find the data mount point dynamically
        local data_mount=""
        while IFS= read -r line; do
            if [[ "$line" =~ ${device}s2 ]]; then
                data_mount=$(echo "$line" | awk '{print $3}')
                break
            fi
        done < <(mount | grep "^${device}s2")
        
        if [[ -z "$bootfs_mount" ]]; then
            print_error "Could not find mount point for boot partition"
            exit 1
        fi
        
        if [[ -z "$data_mount" ]]; then
            print_error "Could not find mount point for data partition"
            exit 1
        fi
        
        print_success "Partitions mounted:"
        print_info "  Boot: $bootfs_mount"
        print_info "  Data: $data_mount"
        
        # Call the existing migrate_images function
        migrate_images "$device" "$bootfs_mount" "$data_mount"
        
        print_success "Migration complete!"
    else
        usage
    fi
}

main "$@"