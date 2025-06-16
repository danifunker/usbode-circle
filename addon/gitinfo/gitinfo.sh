#!/bin/bash

# Get git information
BRANCH=$(git rev-parse --abbrev-ref HEAD 2>/dev/null || echo "unknown")
COMMIT=$(git rev-parse --short HEAD 2>/dev/null || echo "unknown")
#DIRTY=$(git diff --quiet 2>/dev/null || echo "-dirty")

# Version information - could be passed as parameters or stored in a version file
MAJOR_VERSION="2"
MINOR_VERSION="1"
PATCH_VERSION="0"

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
    
    // Get git information
    const char* GetBranch(void) const;
    const char* GetCommit(void) const;
    
    // Get formatted version strings
    const char* GetVersionString(void) const;
    const char* GetFullVersionString(void) const; // Includes build date/time
    
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
    
    // Git information
    const char* m_GitBranch;
    const char* m_GitCommit;
    
    // Formatted version strings
    CString m_FormattedVersion;
    CString m_FullFormattedVersion;
};

#endif
EOF

echo "Generated gitinfo.h with branch ${BRANCH}, commit ${COMMIT}${DIRTY}, and version ${MAJOR_VERSION}.${MINOR_VERSION}.${PATCH_VERSION}"