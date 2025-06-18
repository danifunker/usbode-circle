#!/bin/bash

# Get git information
BRANCH=$(git rev-parse --abbrev-ref HEAD 2>/dev/null || echo "unknown")
COMMIT=$(git rev-parse --short HEAD 2>/dev/null || echo "unknown")
#DIRTY=$(git diff --quiet 2>/dev/null || echo "-dirty")

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
    BUILD_NUMBER="-${USBODE_BUILD_NUMBER}"
    echo "Using build number: $USBODE_BUILD_NUMBER"
fi

# Create header file with git and version info
cat > ./gitinfo.h << EOF
// Auto-generated file - Do not edit
#ifndef _gitinfo_h
#define _gitinfo_h

#include <circle/types.h>
#include <circle/string.h>

// Git information
#define GIT_BRANCH "${BRANCH}"
#define GIT_COMMIT "${COMMIT}${DIRTY}"

// Version information
#define VERSION_MAJOR "${MAJOR_VERSION}"
#define VERSION_MINOR "${MINOR_VERSION}"
#define VERSION_PATCH "${PATCH_VERSION}"
#define BUILD_NUMBER "${BUILD_NUMBER}"

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
    
    // Get formatted version strings
    const char* GetVersionString(void) const;
    const char* GetVersionWithBuildString(void) const; // Version with build number only
    const char* GetFullVersionString(void) const; // Includes build date/time
    const char* GetShortVersionString(void) const; // For displays (max 18 chars)
    
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
    
    // Formatted version strings
    CString m_FormattedVersion;
    CString m_VersionWithBuildString;
    CString m_FullFormattedVersion;
    CString m_ShortVersionString;
};

#endif
EOF

echo "Generated gitinfo.h with branch ${BRANCH}, commit ${COMMIT}${DIRTY}, version ${MAJOR_VERSION}.${MINOR_VERSION}.${PATCH_VERSION}${BUILD_NUMBER}"