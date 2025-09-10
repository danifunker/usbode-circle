#!/bin/bash
# filepath: /Users/dani/repos/usbode-circle/update-os-sublist.sh

set -ex

# Configuration
GITHUB_REPO="danifunker/usbode-circle"
JSON_FILE="os-sublist.json"
TEMP_DIR=$(mktemp -d)
DEBUG_FILE="$TEMP_DIR/release_data.json"

# Cleanup
trap 'rm -rf "$TEMP_DIR"' EXIT

# Cross-platform file operations
get_file_size() {
    [[ "$OSTYPE" == "darwin"* ]] && stat -f%z "$1" || stat -c%s "$1"
}

calculate_sha256() {
    [[ "$OSTYPE" == "darwin"* ]] && shasum -a 256 "$1" | cut -d' ' -f1 || sha256sum "$1" | cut -d' ' -f1
}

# Fetch release data from GitHub API
get_release_data() {
    local release_tag="$1"
    local api_url="https://api.github.com/repos/$GITHUB_REPO/releases/tags/$release_tag"
    
    local data=$(curl -sf -H "Accept: application/vnd.github.v3+json" "$api_url")
    
    # Save for debugging and validate
    echo "$data" > "$DEBUG_FILE"
    echo "$data" | jq empty || { echo "GitHub API error - check $DEBUG_FILE"; exit 1; }
    
    local error=$(echo "$data" | jq -r '.message // null')
    [[ "$error" != "null" ]] && { echo "API Error: $error"; exit 1; }
    
    echo "$data"
}

# Process a single .img.xz asset URL
process_asset() {
    local url="$1"
    local filename=$(basename "$url")
    local filepath="$TEMP_DIR/$filename"
    
    curl -sfL "$url" -o "$filepath"
    
    local download_size=$(get_file_size "$filepath")
    local download_sha256=$(calculate_sha256 "$filepath")
    
    local extracted_file="$TEMP_DIR/${filename%.xz}"
    xz -dc "$filepath" > "$extracted_file"
    
    local extract_size=$(get_file_size "$extracted_file")
    local extract_sha256=$(calculate_sha256 "$extracted_file")
    
    jq -n \
        --arg url "$url" \
        --argjson download_size "$download_size" \
        --arg download_sha256 "$download_sha256" \
        --argjson extract_size "$extract_size" \
        --arg extract_sha256 "$extract_sha256" \
        '{
            url: $url,
            image_download_size: $download_size,
            image_download_sha256: $download_sha256,
            extract_size: $extract_size,
            extract_sha256: $extract_sha256
        }'
}

# Main execution
main() {
    [[ $# -ne 1 ]] && { echo "Usage: $0 <release-tag>"; exit 1; }
    
    # Dependency check
    for tool in curl jq xz; do
        command -v "$tool" >/dev/null || { echo "Missing: $tool"; exit 1; }
    done
    
    [[ ! -f "$JSON_FILE" ]] && { echo "Missing: $JSON_FILE"; exit 1; }
    
    local release_tag="$1"
    local release_data=$(get_release_data "$release_tag")
    
    # Extract metadata
    local release_date=$(echo "$release_data" | jq -r '.published_at | split("T")[0]')
    
    # Find assets
    local url_32bit=$(echo "$release_data" | jq -r '.assets[] | select(.name | test("\\.img\\.xz$") and (test("-64bit") | not)) | .browser_download_url')
    local url_64bit=$(echo "$release_data" | jq -r '.assets[] | select(.name | test("-64bit\\.img\\.xz$")) | .browser_download_url')
    
    [[ -z "$url_32bit" ]] && { echo "No 32-bit asset found"; exit 1; }
    [[ -z "$url_64bit" ]] && { echo "No 64-bit asset found"; exit 1; }
    
    # Process assets
    local result_32bit=$(process_asset "$url_32bit")
    local result_64bit=$(process_asset "$url_64bit")
    
    # Update JSON
    cp "$JSON_FILE" "$JSON_FILE.backup"
    
    jq --arg release_date "$release_date" \
       --argjson result_32 "$result_32bit" \
       --argjson result_64 "$result_64bit" \
       '
       .os_list[0].url = $result_32.url |
       .os_list[0].release_date = $release_date |
       .os_list[0].extract_size = $result_32.extract_size |
       .os_list[0].extract_sha256 = $result_32.extract_sha256 |
       .os_list[0].image_download_size = $result_32.image_download_size |
       .os_list[0].image_download_sha256 = $result_32.image_download_sha256 |
       .os_list[1].url = $result_64.url |
       .os_list[1].release_date = $release_date |
       .os_list[1].extract_size = $result_64.extract_size |
       .os_list[1].extract_sha256 = $result_64.extract_sha256 |
       .os_list[1].image_download_size = $result_64.image_download_size |
       .os_list[1].image_download_sha256 = $result_64.image_download_sha256
       ' "$JSON_FILE" > "$JSON_FILE.tmp" && mv "$JSON_FILE.tmp" "$JSON_FILE"
    
    echo "Updated $JSON_FILE for $release_tag"
}

main "$@"