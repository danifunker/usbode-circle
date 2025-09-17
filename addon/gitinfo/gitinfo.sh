#!/bin/bash

# Get git information
BRANCH=$(git rev-parse --abbrev-ref HEAD 2>/dev/null || echo "unknown")
COMMIT=$(git rev-parse --short HEAD 2>/dev/null || echo "unknown")
DIRTY=$(git diff --quiet 2>/dev/null || echo "-dirty")

# Path to version.txt file (relative to repository root)
VERSION_FILE="../../version.txt"

# Default version values in case the file doesn't exist or can't be read
MAJOR_VERSION="0"
MINOR_VERSION="0"
PATCH_VERSION="0"

# Check if version.txt exists and read from it
if [ -f "$VERSION_FILE" ]; then
    echo "Reading version from $VERSION_FILE"
    
    # Read the version string from the file (expected format: X.Y.Z)
    VERSION_STRING=$(cat "$VERSION_FILE" | tr -d '\r\n')
    
    # Parse the version components
    if [[ $VERSION_STRING =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)$ ]]; then
        MAJOR_VERSION="${BASH_REMATCH[1]}"
        MINOR_VERSION="${BASH_REMATCH[2]}"
        PATCH_VERSION="${BASH_REMATCH[3]}"
        echo "Parsed version: $MAJOR_VERSION.$MINOR_VERSION.$PATCH_VERSION"
    else
        echo "Warning: Invalid version format in $VERSION_FILE. Using defaults."
    fi
else
    echo "Warning: $VERSION_FILE not found. Using default version."
fi

# Check for build number from environment variable (set by build script)
BUILD_NUMBER=""
if [ -n "$USBODE_BUILD_NUMBER" ]; then
    BUILD_NUMBER="${USBODE_BUILD_NUMBER}"
    echo "Using build number: $USBODE_BUILD_NUMBER"
fi

# Get architecture and platform information from environment/makefile variables
AARCH="${AARCH:-32}"
RASPPI="${RASPPI:-1}"

# Determine architecture type
if [ "$AARCH" = "64" ]; then
    ARCH_TYPE="AARCH64"
else
    ARCH_TYPE="AARCH32"
fi

# Determine kernel target name based on AARCH and RASPPI values
if [ "$AARCH" = "32" ]; then
    case "$RASPPI" in
        1) KERNEL_TARGET="kernel.img" ;;
        2) KERNEL_TARGET="kernel7.img" ;;
        3) KERNEL_TARGET="kernel8-32.img" ;;
        4) KERNEL_TARGET="kernel7l.img" ;;
        *) KERNEL_TARGET="unknown kernel 32" ;;
    esac
else # AARCH=64
    case "$RASPPI" in
        3) KERNEL_TARGET="kernel8.img" ;;
        4) KERNEL_TARGET="kernel8-rpi4.img" ;;
        5) KERNEL_TARGET="kernel_2712.img" ;;
        *) KERNEL_TARGET="unknown kernel 64" ;;
    esac
fi

# Create header file with git, version, and architecture info (including buildtime.h)
cat > ./gitinfo.h << EOF
// Auto-generated file - Do not edit
#ifndef _gitinfo_h
#define _gitinfo_h

#include <circle/types.h>
#include <circle/string.h>
#include "buildtime.h"

// Git information
#define GIT_BRANCH "${BRANCH}"
#define GIT_COMMIT "${COMMIT}${DIRTY}"

// Version information
#define VERSION_MAJOR "${MAJOR_VERSION}"
#define VERSION_MINOR "${MINOR_VERSION}"
#define VERSION_PATCH "${PATCH_VERSION}"
#define BUILD_NUMBER "${BUILD_NUMBER}"

// Architecture and platform information
#define ARCH_TYPE "${ARCH_TYPE}"
#define KERNEL_TARGET "${KERNEL_TARGET}"
#define AARCH_BITS "${AARCH}"
#define RASPPI_MODEL "${RASPPI}"

class CGitInfo
{
public:
    // Singleton accessor
    static CGitInfo* Get(void);
    
    // Destructor
    ~CGitInfo(void);
    
    // Get version components
    const char* GetMajorVersion(void) const;
    const char* GetMinorVersion(void) const;
    const char* GetPatchVersion(void) const;
    const char* GetBuildNumber(void) const;
    
    // Get git information
    const char* GetBranch(void) const;
    const char* GetCommit(void) const;
    
    // Get build timestamp information
    const char* GetBuildTimestamp(void) const;
    const char* GetBuildTimestampISO(void) const;
    const char* GetBuildDate(void) const;
    const char* GetBuildTime(void) const;
    const char* GetBuildUnixTimestamp(void) const;
    
    // Get replacements for __DATE__ and __TIME__
    const char* GetBuildDateCompact(void) const;
    const char* GetBuildTimeCompact(void) const;
    
    // Get architecture and platform information
    const char* GetArchType(void) const;
    const char* GetKernelName(void) const;
    const char* GetArchBits(void) const;
    const char* GetRaspPiModel(void) const;
    bool Is64Bit(void) const;
    
    // Get formatted version strings
    const char* GetVersionString(void) const;
    const char* GetVersionWithBuildString(void) const; // Version with build number only
    const char* GetFullVersionString(void) const; // Includes build date/time
    const char* GetShortVersionString(void) const; // For displays (max 18 chars)
    const char* GetPlatformString(void) const; // Architecture and model info
    
private:
    // Private constructor (singleton pattern)
    CGitInfo(void);
    
    // Generate the formatted version strings
    void UpdateFormattedVersions(void);
    
private:
    // Static singleton instance
    static CGitInfo* s_pThis;
    
    // Version components
    const char* m_MajorVersion;
    const char* m_MinorVersion;
    const char* m_PatchVersion;
    const char* m_BuildNumber;
    
    // Git information
    const char* m_GitBranch;
    const char* m_GitCommit;
    
    // Architecture and platform information
    const char* m_ArchType;
    const char* m_KernelTarget;
    const char* m_ArchBits;
    const char* m_RaspPiModel;
    
    // Formatted version strings
    CString m_FormattedVersion;
    CString m_VersionWithBuildString;
    CString m_FullFormattedVersion;
    CString m_ShortVersionString;
    CString m_PlatformString;
};

#endif
EOF

echo "Generated gitinfo.h with:"
echo "  Branch: ${BRANCH}"
echo "  Commit: ${COMMIT}${DIRTY}"
echo "  Version: ${MAJOR_VERSION}.${MINOR_VERSION}.${PATCH_VERSION}${BUILD_NUMBER}"
echo "  Architecture: ${ARCH_TYPE} (${AARCH}-bit)"
echo "  Raspberry Pi: Model ${RASPPI}"
echo "  Kernel Target: ${KERNEL_TARGET}"