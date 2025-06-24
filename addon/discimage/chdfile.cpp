#include "chdfile.h"
#include <assert.h>
#include <circle/stdarg.h>
#include <circle/util.h>
#include <stdlib.h>
#include <string.h>

LOGMODULE("CCHDFileDevice");

CCHDFileDevice::CCHDFileDevice(FIL *pFile) 
    : CCueBinFileDevice(pFile, nullptr)  // Call parent constructor
{
    m_FileType = FileType::CHD;  // Set file type to CHD
    
    // Initialize CHD parser
    if (!m_chdParser.initialize(pFile)) {
        LOGERR("Failed to initialize CHD parser");
    }
    
    // Generate CUE sheet from CHD metadata if available
    const char* cueSheet = m_chdParser.generateCueSheet();
    if (cueSheet) {
        // If we have a cue sheet from the CHD parser, use it
        size_t len = strlen(cueSheet);
        if (m_cue_str) {
            delete[] m_cue_str;  // Delete the default cue sheet
        }
        m_cue_str = new char[len + 1];
        strcpy(m_cue_str, cueSheet);
    }
}

CCHDFileDevice::~CCHDFileDevice(void) 
{
    // Parent destructor will handle cleanup
}

int CCHDFileDevice::Read(void *pBuffer, size_t nCount) 
{
    // If CHD parser is working, use it for data access
    // Otherwise fall back to parent implementation
    return CCueBinFileDevice::Read(pBuffer, nCount);
}

u64 CCHDFileDevice::Seek(u64 ullOffset) 
{
    // Use parent implementation for now
    return CCueBinFileDevice::Seek(ullOffset);
}