//
// A SCSCI Toolbox Service
//
// This is the entry point for listing and mounting CD images. All parts
// of USBODE will use this, not just the SCSI Toolbox
//
//
// Copyright (C) 2025 Ian Cass
// Copyright (C) 2025 Dani Sarfati
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
#include <circle/logger.h>
#include "scsitbservice.h"
#include <circle/sched/scheduler.h>
#include <cstdlib>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <discimage/cuebinfile.h>
#include <discimage/mdsfile.h>
#include <discimage/cuedevice.h>
#include <discimage/util.h>

LOGMODULE("scsitbservice");

static size_t my_strnlen(const char *s, size_t maxlen) {
    size_t i;
    for (i = 0; i < maxlen; i++) {
        if (s[i] == '\0') break;
    }
    return i;
}

static bool iequals(const char* a, const char* b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return false;
        ++a; ++b;
    }
    return *a == *b;
}

int compareFileEntries(const void* a, const void* b) {
    const FileEntry* fa = (const FileEntry*)a;
    const FileEntry* fb = (const FileEntry*)b;
    return strcasecmp(fa->name, fb->name);
}

SCSITBService *SCSITBService::s_pThis = 0;

SCSITBService::SCSITBService()
{
    LOGNOTE("SCSITBService::SCSITBService() called");
    
    // I am the one and only!
    assert(s_pThis == 0);
    s_pThis = this;

    cdromservice = static_cast<CDROMService*>(CScheduler::Get()->GetTask("cdromservice"));
    configservice = static_cast<ConfigService*>(CScheduler::Get()->GetTask("configservice"));
    assert(cdromservice != nullptr && "Failed to get cdromservice");

    m_FileEntries = new FileEntry[MAX_FILES]();
    bool ok = RefreshCache();
    assert(ok && "Failed to refresh SCSITBService on construction");
    SetName("scsitbservice");
}

SCSITBService::~SCSITBService() {
    delete[] m_FileEntries;
    m_FileEntries = nullptr;
}

size_t SCSITBService::GetCount() const {
    return m_FileCount;
}

const char* SCSITBService::GetName(size_t index) const {
    if (index >= m_FileCount)
        return nullptr;
    return m_FileEntries[index].name;
}

DWORD SCSITBService::GetSize(size_t index) const {
    if (index >= m_FileCount)
        return 0;
    return m_FileEntries[index].size;
}

FileEntry* SCSITBService::begin() { return m_FileEntries; }
FileEntry* SCSITBService::end() { return m_FileEntries + m_FileCount; }
const FileEntry* SCSITBService::GetFileEntry(size_t index) const {
    if (index >= m_FileCount)
        return nullptr;
    return &m_FileEntries[index];
}

size_t SCSITBService::GetCurrentCD() {
	return current_cd;
}

bool SCSITBService::SetNextCD(size_t cd) {
    //TODO bounds checking
    next_cd = cd;
    return true;
}

const char* SCSITBService::GetCurrentCDName() {
	return GetName(GetCurrentCD());
}

bool SCSITBService::SetNextCDByName(const char* file_name) {

	int index = 0;
        for (const FileEntry* it = begin(); it != end(); ++it, ++index) {
		//LOGNOTE("SCSITBService::SetNextCDByName testing %s", it->name);
                if (strcmp(file_name, it->name) == 0) {
                        return SetNextCD(index);
                }
        }

	return false;
}


bool SCSITBService::RefreshCache() {
    LOGNOTE("SCSITBService::RefreshCache() called");
    m_Lock.Acquire ();

    // Get current loaded image
    const char* current_image = configservice->GetCurrentImage(DEFAULT_IMAGE_FILENAME);
    LOGNOTE("SCSITBService::RefreshCache() loaded current_image %s from config.txt", current_image);

    m_FileCount = 0;

    // Read our directory of images
    // and populate m_FileEntries
    DIR dir;
    FRESULT fr = f_opendir(&dir, "1:/");
    if (fr != FR_OK) {
        // TODO: handle error as needed
        return false;
    }

    LOGNOTE("SCSITBService::RefreshCache() opened directory");
    FILINFO fno;
    while (true) {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK || fno.fname[0] == 0)
            break;

        if (strcmp(fno.fname, ".") == 0 || strcmp(fno.fname, "..") == 0)
            continue;

        // Exclude Mac cache files
        if (strncmp(fno.fname, "._", 2) == 0 || strcmp(fno.fname, ".DS_Store") == 0)
            continue;            

	//LOGNOTE("SCSITBService::RefreshCache() found file %s", fno.fname);
        const char* ext = strrchr(fno.fname, '.');
        if (ext != nullptr) {
            if (iequals(ext, ".iso") || iequals(ext, ".bin") || iequals(ext, ".mds")) {
		if (m_FileCount >= MAX_FILES)
                    break;
		//LOGNOTE("SCSITBService::RefreshCache() adding file %s to m_FileEntries", fno.fname);
                size_t len = my_strnlen(fno.fname, MAX_FILENAME_LEN - 1);
                memcpy(m_FileEntries[m_FileCount].name, fno.fname, len);
                m_FileEntries[m_FileCount].name[len] = '\0';
                m_FileEntries[m_FileCount].size = fno.fsize;
                m_FileCount++;
            }
        }
    }

    // Sort m_FileEntries by filename alphabetically
    if (m_FileCount > 1)
        qsort(m_FileEntries, m_FileCount, sizeof(m_FileEntries[0]), compareFileEntries);

    // Find the index of current_image in m_FileEntries
    for (size_t i = 0; i < m_FileCount; ++i) {
	//LOGNOTE("i is %u, m_FileCount is %u, current_image is %s, m_FileEntries is %s", i, m_FileCount, current_image, m_FileEntries[i].name);
        if (strcmp(m_FileEntries[i].name, current_image) == 0) {
	    
	    // If we don't yet have a current_cd e.g. we've 
	    // just booted, then mount it
	    if (current_cd < 0)  {
		next_cd = i;
	    } else {
            	current_cd = i;
	    }
		    
            break;
        }
    }

    //TODO handle case where we can't find the CD in the list, fall back to 
    //the default image

    LOGNOTE("SCSITBService::RefreshCache() Found %d images (.ISO and BIN total)", m_FileCount);

    f_closedir(&dir);
    m_Lock.Release ();
    return true;
}

void SCSITBService::Run() {
	LOGNOTE("SCSITBService::Run started");

	while (true) {

		m_Lock.Acquire ();
		// Do we have a next cd?
		if (next_cd > -1) {

			// Check if it's valid
			if ((size_t)next_cd > m_FileCount) {
				next_cd = -1;
				continue;
			}

			// Load it
			char* imageName = m_FileEntries[next_cd].name;
			ICueDevice* cueBinFileDevice = loadCueBinFileDevice(imageName);

			if (cueBinFileDevice == nullptr) {
				LOGERR("Failed to load image: %s", imageName);
				next_cd = -1;
				continue;
			}
			
			// Set the new device in the CD gadget
    			cdromservice->SetDevice(cueBinFileDevice);

			// Save current mounted image name
			// TODO only if different
			configservice->SetCurrentImage(imageName);

			current_cd = next_cd;

			// Mark done
			next_cd = -1;
		}
		m_Lock.Release ();
		CScheduler::Get()->MsSleep(100);
	}
}
