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
    , m_BuildNumber(BUILD_NUMBER)
    , m_GitBranch(GIT_BRANCH)
    , m_GitCommit(GIT_COMMIT)
    , m_ArchType(ARCH_TYPE)
    , m_KernelTarget(KERNEL_TARGET)
    , m_ArchBits(AARCH_BITS)
    , m_RaspPiModel(RASPPI_MODEL)
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

const char* CGitInfo::GetBuildNumber(void) const
{
    return m_BuildNumber;
}

const char* CGitInfo::GetBranch(void) const
{
    return m_GitBranch;
}

const char* CGitInfo::GetCommit(void) const
{
    return m_GitCommit;
}

const char* CGitInfo::GetArchType(void) const
{
    return m_ArchType;
}

const char* CGitInfo::GetKernelName(void) const
{
    return m_KernelTarget;
}

const char* CGitInfo::GetArchBits(void) const
{
    return m_ArchBits;
}

const char* CGitInfo::GetRaspPiModel(void) const
{
    return m_RaspPiModel;
}

bool CGitInfo::Is64Bit(void) const
{
    return (strcmp(m_ArchBits, "64") == 0);
}

const char* CGitInfo::GetVersionString(void) const
{
    return m_FormattedVersion;
}

const char* CGitInfo::GetVersionWithBuildString(void) const
{
    return m_VersionWithBuildString;
}

const char* CGitInfo::GetFullVersionString(void) const
{
    return m_FullFormattedVersion;
}

const char* CGitInfo::GetShortVersionString(void) const
{
    return m_ShortVersionString;
}

const char* CGitInfo::GetPlatformString(void) const
{
    return m_PlatformString;
}

void CGitInfo::UpdateFormattedVersions(void)
{
    // Create the base version string (x.y.z)
    CString baseVersion;
    baseVersion.Format("%s.%s.%s", m_MajorVersion, m_MinorVersion, m_PatchVersion);
    
    // Add build number if present (with dash for display)
    if (strlen(m_BuildNumber) > 0)
    {
        CString versionWithBuild;
        versionWithBuild.Format("%s-%s", (const char*)baseVersion, m_BuildNumber);
        baseVersion = versionWithBuild;
    }
    
    // Store the version with build number (without git hash)
    m_VersionWithBuildString = baseVersion;
    
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
                                m_ArchType,
                                __DATE__, __TIME__);
    
    // Create platform string
    m_PlatformString.Format("%s Pi %s (%s)", 
                           m_ArchType, 
                           m_RaspPiModel,
                           m_KernelTarget);
    
    // Create a very short version string for display (18 chars max)
    CString shortVersionBase;
    shortVersionBase.Format("%s.%s.%s", m_MajorVersion, m_MinorVersion, m_PatchVersion);
    
    // Add build number to short version if present
    if (strlen(m_BuildNumber) > 0)
    {
        CString shortVersionWithBuild;
        shortVersionWithBuild.Format("%s-%s", (const char*)shortVersionBase, m_BuildNumber);
        shortVersionBase = shortVersionWithBuild;
    }
    
    m_ShortVersionString.Format("USBODE v%s", (const char*)shortVersionBase);
    
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