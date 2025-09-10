#!/bin/bash
# filepath: update-os-sublist.sh

set -e

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
GITHUB_REPO="danifunker/usbode-circle"
JSON_FILE="os-sublist.json"
TEMP_DIR=$(mktemp -d)

# Cleanup function
cleanup() {
    echo -e "${BLUE}Cleaning up temporary files...${NC}"
    rm -rf "$TEMP_DIR"
}
trap cleanup EXIT

# Cross-platform compatibility functions
get_file_size() {
    local file="$1"
    if [[ "$OSTYPE" == "darwin"* ]]; then
        stat -f%z "$file"
    else
        stat -c%s "$file"
    fi
}

calculate_sha256() {
    local file="$1"
    if [[ "$OSTYPE" == "darwin"* ]]; then
        shasum -a 256 "$file" | cut -d' ' -f1
    else
        sha256sum "$file" | cut -d' ' -f1
    fi
}

# Function to get release info from GitHub API
get_release_info() {
    local release_tag="$1"
    echo -e "${BLUE}Fetching release information for $release_tag...${NC}"
    
    local api_url="https://api.github.com/repos/$GITHUB_REPO/releases/tags/$release_tag"
    local release_data=$(curl -s "$api_url")
    
    if echo "$release_data" | grep -q '"message": "Not Found"'; then
        echo -e "${RED}Error: Release $release_tag not found${NC}"
        exit 1
    fi
    
    echo "$release_data"
}

# Function to extract version from release tag
extract_version() {
    local release_tag="$1"
    local release_data="$2"
    
    # Try to get version from tag name first (e.g., v2.6.0)
    if [[ "$release_tag" =~ ^v([0-9]+\.[0-9]+\.[0-9]+) ]]; then
        echo "${BASH_REMATCH[1]}"
        return
    fi
    
    # Try to extract from release name
    local release_name=$(echo "$release_data" | jq -r '.name // empty')
    if [[ "$release_name" =~ ([0-9]+\.[0-9]+\.[0-9]+) ]]; then
        echo "${BASH_REMATCH[1]}"
        return
    fi
    
    # Fallback to extracting from assets
    local asset_name=$(echo "$release_data" | jq -r '.assets[0].name // empty')
    if [[ "$asset_name" =~ usbode-([0-9]+\.[0-9]+\.[0-9]+) ]]; then
        echo "${BASH_REMATCH[1]}"
        return
    fi
    
    echo "unknown"
}

# Function to process a single architecture
process_architecture() {
    local release_data="$1"
    local arch="$2"
    local build_suffix="$3"
    
    echo -e "${YELLOW}Processing $arch architecture...${NC}"
    
    # Find the correct asset
    local download_url
    if [[ "$arch" == "32-bit" ]]; then
        download_url=$(echo "$release_data" | jq -r --arg suffix "$build_suffix" '.assets[] | select(.name | test("usbode.*\\.img\\.xz$") and (test("64bit") | not)) | .browser_download_url')
    else
        download_url=$(echo "$release_data" | jq -r --arg suffix "$build_suffix" '.assets[] | select(.name | test("usbode.*64bit.*\\.img\\.xz$")) | .browser_download_url')
    fi
    
    if [[ -z "$download_url" || "$download_url" == "null" ]]; then
        echo -e "${RED}Error: Could not find $arch asset for this release${NC}"
        exit 1
    fi
    
    local filename=$(basename "$download_url")
    local filepath="$TEMP_DIR/$filename"
    
    echo -e "${BLUE}Downloading $filename...${NC}"
    curl -L -o "$filepath" "$download_url"
    
    # Calculate download size and SHA256
    local download_size=$(get_file_size "$filepath")
    local download_sha256=$(calculate_sha256 "$filepath")
    
    echo -e "${BLUE}Extracting $filename...${NC}"
    local extracted_file="$TEMP_DIR/${filename%.xz}"
    xz -d -c "$filepath" > "$extracted_file"
    
    # Calculate extracted size and SHA256
    local extract_size=$(get_file_size "$extracted_file")
    local extract_sha256=$(calculate_sha256 "$extracted_file")
    
    echo -e "${GREEN}$arch calculations complete:${NC}"
    echo "  Download size: $download_size bytes"
    echo "  Download SHA256: $download_sha256"
    echo "  Extract size: $extract_size bytes"
    echo "  Extract SHA256: $extract_sha256"
    
    # Return values as a JSON object
    cat << EOF
{
    "url": "$download_url",
    "image_download_size": $download_size,
    "image_download_sha256": "$download_sha256",
    "extract_size": $extract_size,
    "extract_sha256": "$extract_sha256"
}
EOF
}

# Main function
main() {
    if [[ $# -ne 1 ]]; then
        echo "Usage: $0 <release-tag>"
        echo "Example: $0 build-465"
        echo "Example: $0 v2.6.0"
        exit 1
    fi
    
    local release_tag="$1"
    
    # Check if required tools are available
    for tool in curl jq xz; do
        if ! command -v "$tool" &> /dev/null; then
            echo -e "${RED}Error: $tool is required but not installed${NC}"
            exit 1
        fi
    done
    
    # Check if JSON file exists
    if [[ ! -f "$JSON_FILE" ]]; then
        echo -e "${RED}Error: $JSON_FILE not found${NC}"
        exit 1
    fi
    
    # Get release information
    local release_data=$(get_release_info "$release_tag")
    local release_date=$(echo "$release_data" | jq -r '.published_at | split("T")[0]')
    local version=$(extract_version "$release_tag" "$release_data")
    
    echo -e "${GREEN}Release Date: $release_date${NC}"
    echo -e "${GREEN}Version: $version${NC}"
    
    # Process both architectures
    local result_32bit=$(process_architecture "$release_data" "32-bit" "")
    local result_64bit=$(process_architecture "$release_data" "64-bit" "64bit")
    
    # Extract values from results
    local url_32=$(echo "$result_32bit" | jq -r '.url')
    local download_size_32=$(echo "$result_32bit" | jq -r '.image_download_size')
    local download_sha_32=$(echo "$result_32bit" | jq -r '.image_download_sha256')
    local extract_size_32=$(echo "$result_32bit" | jq -r '.extract_size')
    local extract_sha_32=$(echo "$result_32bit" | jq -r '.extract_sha256')
    
    local url_64=$(echo "$result_64bit" | jq -r '.url')
    local download_size_64=$(echo "$result_64bit" | jq -r '.image_download_size')
    local download_sha_64=$(echo "$result_64bit" | jq -r '.image_download_sha256')
    local extract_size_64=$(echo "$result_64bit" | jq -r '.extract_size')
    local extract_sha_64=$(echo "$result_64bit" | jq -r '.extract_sha256')
    
    # Update JSON file
    echo -e "${BLUE}Updating $JSON_FILE...${NC}"
    
    # Create backup
    cp "$JSON_FILE" "$JSON_FILE.backup"
    
    # Update the JSON file using jq
    jq --arg version "$version" \
       --arg release_date "$release_date" \
       --arg url_32 "$url_32" \
       --argjson download_size_32 "$download_size_32" \
       --arg download_sha_32 "$download_sha_32" \
       --argjson extract_size_32 "$extract_size_32" \
       --arg extract_sha_32 "$extract_sha_32" \
       --arg url_64 "$url_64" \
       --argjson download_size_64 "$download_size_64" \
       --arg download_sha_64 "$download_sha_64" \
       --argjson extract_size_64 "$extract_size_64" \
       --arg extract_sha_64 "$extract_sha_64" \
       '
       .os_list[0].name = "USBODE 32-bit" |
       .os_list[0].url = $url_32 |
       .os_list[0].release_date = $release_date |
       .os_list[0].extract_size = $extract_size_32 |
       .os_list[0].extract_sha256 = $extract_sha_32 |
       .os_list[0].image_download_size = $download_size_32 |
       .os_list[0].image_download_sha256 = $download_sha_32 |
       .os_list[1].name = "USBODE 64-bit" |
       .os_list[1].url = $url_64 |
       .os_list[1].release_date = $release_date |
       .os_list[1].extract_size = $extract_size_64 |
       .os_list[1].extract_sha256 = $extract_sha_64 |
       .os_list[1].image_download_size = $download_size_64 |
       .os_list[1].image_download_sha256 = $download_sha_64
       ' "$JSON_FILE" > "$JSON_FILE.tmp" && mv "$JSON_FILE.tmp" "$JSON_FILE"
    
    echo -e "${GREEN}Successfully updated $JSON_FILE${NC}"
    echo -e "${BLUE}Backup saved as $JSON_FILE.backup${NC}"
    
    # Show what changed
    echo -e "${YELLOW}Summary of changes:${NC}"
    echo "  Release Date: $release_date"
    echo "  32-bit URL: $url_32"
    echo "  64-bit URL: $url_64"
}

main "$@"