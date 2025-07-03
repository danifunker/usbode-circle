#!/bin/bash
# filepath: /Users/dani/repos/usbode-circle/scripts/apply-patches.sh

set -e

PROJECT_ROOT=$(git rev-parse --show-toplevel)
PATCHES_DIR="$PROJECT_ROOT/patches"

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Function to apply patches for a specific submodule
apply_patches_for_submodule() {
    local submodule_name="$1"
    local submodule_path="$2"
    local patches_subdir="$PATCHES_DIR/$submodule_name"
    
    if [ ! -d "$patches_subdir" ]; then
        log_info "No patches directory found for $submodule_name, skipping..."
        return 0
    fi
    
    if [ ! -d "$submodule_path" ]; then
        log_error "Submodule path $submodule_path does not exist!"
        return 1
    fi
    
    # Find all .patch files and sort them
    local patch_files=($(find "$patches_subdir" -name "*.patch" | sort))
    
    if [ ${#patch_files[@]} -eq 0 ]; then
        log_info "No patch files found for $submodule_name"
        return 0
    fi
    
    log_info "Applying patches for $submodule_name..."
    
    cd "$submodule_path"
    
    # Check if patches are already applied by looking for a marker file
    local marker_file=".patches_applied"
    if [ -f "$marker_file" ]; then
        log_warn "Patches already applied for $submodule_name (marker file exists)"
        cd "$PROJECT_ROOT"
        return 0
    fi
    
    # Apply each patch
    for patch_file in "${patch_files[@]}"; do
        local patch_name=$(basename "$patch_file")
        log_info "  Applying $patch_name..."
        
        # First check if patch can be applied
        if git apply --check "$patch_file" >/dev/null 2>&1; then
            git apply "$patch_file"
            log_info "    ✓ Successfully applied $patch_name"
        elif git apply --reverse --check "$patch_file" >/dev/null 2>&1; then
            log_warn "    ~ Patch $patch_name appears to already be applied, skipping..."
        else
            log_error "    ✗ Failed to apply $patch_name"
            log_error "    Debug info:"
            log_error "      Current directory: $(pwd)"
            log_error "      Git status:"
            git status --porcelain
            log_error "      Patch check output:"
            git apply --check "$patch_file" 2>&1 || true
            log_error "      Trying with different options..."
            
            # Try with different git apply options
            if git apply --ignore-space-change --ignore-whitespace --check "$patch_file" >/dev/null 2>&1; then
                log_warn "    Applying with whitespace/space change tolerance..."
                git apply --ignore-space-change --ignore-whitespace "$patch_file"
                log_info "    ✓ Successfully applied $patch_name with whitespace tolerance"
            elif git apply -3 --check "$patch_file" >/dev/null 2>&1; then
                log_warn "    Attempting 3-way merge..."
                git apply -3 "$patch_file"
                log_info "    ✓ Successfully applied $patch_name with 3-way merge"
            else
                log_error "    All application methods failed. Patch contents:"
                head -20 "$patch_file"
                cd "$PROJECT_ROOT"
                return 1
            fi
        fi
    done
    
    # Create marker file to indicate patches have been applied
    echo "Patches applied on $(date)" > "$marker_file"
    log_info "✓ All patches applied for $submodule_name"
    
    cd "$PROJECT_ROOT"
    return 0
}

# Function to reset patches for a submodule
reset_patches_for_submodule() {
    local submodule_name="$1"
    local submodule_path="$2"
    
    if [ ! -d "$submodule_path" ]; then
        log_error "Submodule path $submodule_path does not exist!"
        return 1
    fi
    
    cd "$submodule_path"
    
    # Reset to HEAD to remove any applied patches
    if [ -f ".patches_applied" ]; then
        log_info "Resetting patches for $submodule_name..."
        git reset --hard HEAD
        git clean -fd
        rm -f ".patches_applied"
        log_info "✓ Patches reset for $submodule_name"
    else
        log_info "No patches to reset for $submodule_name"
    fi
    
    cd "$PROJECT_ROOT"
}

# Function to get submodule path by name
get_submodule_path() {
    local name="$1"
    case "$name" in
        "circle-stdlib")
            echo "$PROJECT_ROOT/circle-stdlib"
            ;;
        "circle")
            echo "$PROJECT_ROOT/circle-stdlib/libs/circle"
            ;;
        "circle-newlib")
            echo "$PROJECT_ROOT/circle-stdlib/libs/circle-newlib"
            ;;
        *)
            echo ""
            ;;
    esac
}

# Main function
main() {
    local action="${1:-apply}"
    
    # Define submodules as a space-separated list
    local SUBMODULE_NAMES="circle-stdlib circle circle-newlib"
    
    case "$action" in
        "apply")
            log_info "Applying patches to submodules..."
            for submodule_name in $SUBMODULE_NAMES; do
                local submodule_path=$(get_submodule_path "$submodule_name")
                if [ -n "$submodule_path" ]; then
                    apply_patches_for_submodule "$submodule_name" "$submodule_path"
                fi
            done
            log_info "Patch application complete!"
            ;;
        "reset")
            log_info "Resetting patches from submodules..."
            for submodule_name in $SUBMODULE_NAMES; do
                local submodule_path=$(get_submodule_path "$submodule_name")
                if [ -n "$submodule_path" ]; then
                    reset_patches_for_submodule "$submodule_name" "$submodule_path"
                fi
            done
            log_info "Patch reset complete!"
            ;;
        "check")
            log_info "Checking patch status..."
            for submodule_name in $SUBMODULE_NAMES; do
                local submodule_path=$(get_submodule_path "$submodule_name")
                if [ -n "$submodule_path" ] && [ -f "$submodule_path/.patches_applied" ]; then
                    log_info "$submodule_name: Patches applied"
                else
                    log_info "$submodule_name: No patches applied"
                fi
            done
            ;;
        "debug")
            log_info "Debug mode - showing patch info..."
            for submodule_name in $SUBMODULE_NAMES; do
                local submodule_path=$(get_submodule_path "$submodule_name")
                local patches_subdir="$PATCHES_DIR/$submodule_name"
                
                echo "=== $submodule_name ==="
                echo "  Submodule path: $submodule_path"
                echo "  Patches directory: $patches_subdir"
                echo "  Patches directory exists: $([ -d "$patches_subdir" ] && echo "YES" || echo "NO")"
                echo "  Submodule path exists: $([ -d "$submodule_path" ] && echo "YES" || echo "NO")"
                
                if [ -d "$patches_subdir" ]; then
                    echo "  Patch files:"
                    find "$patches_subdir" -name "*.patch" | sort | sed 's/^/    /'
                fi
                
                if [ -d "$submodule_path" ]; then
                    cd "$submodule_path"
                    echo "  Git status:"
                    git status --porcelain | sed 's/^/    /' || echo "    (not a git repository)"
                    cd "$PROJECT_ROOT"
                fi
                echo ""
            done
            ;;
        *)
            echo "Usage: $0 {apply|reset|check|debug}"
            echo "  apply - Apply patches to submodules"
            echo "  reset - Reset patches from submodules"
            echo "  check - Check patch application status"
            echo "  debug - Show debug information about patches and submodules"
            exit 1
            ;;
    esac
}

main "$@"