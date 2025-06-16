#include "gitinfo.h"
#include <circle/logger.h>
#include <circle/util.h>
#include <circle/string.h>

// Initialize static members
CGitInfo* CGitInfo::s_pThis = nullptr;

CGitInfo::CGitInfo(void)
    : m_MajorVersion(VERSION_MAJOR)
    , m_MinorVersion(VERSION_MINOR)
    , m_PatchVersion(VERSION_PATCH)
    , m_GitBranch(GIT_BRANCH)
    , m_GitCommit(GIT_COMMIT)
{
    UpdateFormattedVersions();
}

CGitInfo::~CGitInfo(void)
{
}

CGitInfo* CGitInfo::Get(void)
{
    if (s_pThis == nullptr)
    {
        s_pThis = new CGitInfo();
    }
    
    return s_pThis;
}

const char* CGitInfo::GetMajorVersion(void) const
{
    return m_MajorVersion;
}

const char* CGitInfo::GetMinorVersion(void) const
{
    return m_MinorVersion;
}

const char* CGitInfo::GetPatchVersion(void) const
{
    return m_PatchVersion;
}

const char* CGitInfo::GetBranch(void) const
{
    return m_GitBranch;
}

const char* CGitInfo::GetCommit(void) const
{
    return m_GitCommit;
}

const char* CGitInfo::GetVersionString(void) const
{
    return m_FormattedVersion;
}

const char* CGitInfo::GetFullVersionString(void) const
{
    return m_FullFormattedVersion;
}

const char* CGitInfo::GetShortVersionString(void) const
{
    return m_ShortVersionString;
}

void CGitInfo::UpdateFormattedVersions(void)
{
    // Create the base version string (x.y.z)
    CString baseVersion;
    baseVersion.Format("%s.%s.%s", m_MajorVersion, m_MinorVersion, m_PatchVersion);
    
    // Add branch name only if not on main branch
    if (strcmp(m_GitBranch, "main") != 0)
    {
        CString branchVersion;
        branchVersion.Format("%s-%s", (const char*)baseVersion, m_GitBranch);
        baseVersion = branchVersion;
    }
    
    // Extract first 7 chars of git hash for short version
    CString shortHash;
    if (strlen(m_GitCommit) > 7)
    {
        // Create a temporary buffer for the first 7 chars
        char hashPrefix[8]; // 7 chars + null terminator
        memcpy(hashPrefix, m_GitCommit, 7);
        hashPrefix[7] = '\0'; // Ensure null termination
        shortHash = hashPrefix;
    }
    else
    {
        shortHash = m_GitCommit; // Use the whole string if it's already short
    }
    
    // Create the formatted version string
    m_FormattedVersion.Format("%s-%s", (const char*)baseVersion, (const char*)shortHash);
    
    // Create the full formatted version including build date/time
    m_FullFormattedVersion.Format("%s (built %s %s)", 
                                (const char*)m_FormattedVersion,
                                __DATE__, __TIME__);
    
    // Create a very short version string for display (18 chars max)
    m_ShortVersionString.Format("USBODE v%s.%s.%s", 
                               m_MajorVersion, 
                               m_MinorVersion, 
                               m_PatchVersion);
    
    // Ensure it's never longer than 18 characters
    if (m_ShortVersionString.GetLength() > 18)
    {
        // Truncate and add ellipsis
        char truncated[19]; // 18 + null terminator
        strncpy(truncated, (const char*)m_ShortVersionString, 15);
        strcpy(truncated + 15, "...");
        m_ShortVersionString = truncated;
    }
    
    // Log the version information for debugging
    CLogger::Get()->Write("gitinfo", LogNotice, 
                         "Version: %s, Short: %s, Full: %s", 
                         (const char*)m_FormattedVersion,
                         (const char*)m_ShortVersionString,
                         (const char*)m_FullFormattedVersion);
}