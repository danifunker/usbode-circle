//
// A SCSCI Toolbox Service
//
// This is the entry point for listing and mounting CD images. All parts
// of USBODE will use this, not just the SCSI Toolbox
//
//
// Copyright (C) 2025 Ian Cass
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

SCSITBService::SCSITBService(CPropertiesFatFsFile *pProperties, CUSBCDGadget *pCDGadget) 
: 	m_pProperties (pProperties),
	m_pCDGadget (pCDGadget),
	m_FileCount(0) 
{
    LOGNOTE("SCSITBService::SCSITBService() called");
    
    // I am the one and only!
    assert(s_pThis == 0);
    s_pThis = this;

    m_FileEntries = new FileEntry[MAX_FILES];
    bool ok = RefreshCache();
    assert(ok && "Failed to refresh SCSITBService on construction");
    SetName("scsitbservice");
}

SCSITBService::~SCSITBService() {
    delete[] m_FileEntries;
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

	LOGNOTE("SCSITBService::SetNextCDByName %s", file_name);
	int index = 0;
        for (const FileEntry* it = begin(); it != end(); ++it, ++index) {
		//LOGNOTE("SCSITBService::SetNextCDByName testing %s", it->name);
                if (strcmp(file_name, it->name) == 0) {
			LOGNOTE("SCSITBService::SetNextCDByName found %s", it->name);
                        return SetNextCD(index);
                }
        }

	LOGNOTE("SCSITBService::SetNextCDByName not found");
	return false;
}


bool SCSITBService::RefreshCache() {
    LOGNOTE("SCSITBService::RefreshCache() called");

    // Get current loaded image
    m_pProperties->Load();
    m_pProperties->SelectSection("usbode");
    const char* current_image = m_pProperties->GetString("current_image", DEFAULT_IMAGE_FILENAME);
    LOGNOTE("SCSITBService::RefreshCache() loaded current_image %s from config.txt", current_image);

    m_FileCount = 0;

    DIR dir;
    FRESULT fr = f_opendir(&dir, "/images");
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

	//LOGNOTE("SCSITBService::RefreshCache() found file %s", fno.fname);
        const char* ext = strrchr(fno.fname, '.');
        if (ext != nullptr) {
            if (iequals(ext, ".iso") || iequals(ext, ".bin")) {
		if (m_FileCount >= MAX_FILES)
                    break;
                size_t len = my_strnlen(fno.fname, MAX_FILENAME_LEN - 1);
                memcpy(m_FileEntries[m_FileCount].name, fno.fname, len);
                m_FileEntries[m_FileCount].name[len] = '\0';

                m_FileEntries[m_FileCount].size = fno.fsize;

                m_FileCount++;

            }
        }
    }

    // Sort entries by filename alphabetically
    qsort(m_FileEntries, m_FileCount, sizeof(m_FileEntries[0]), compareFileEntries);

    // Find the index of current_image in m_FileEntries
    for (size_t i = 0; i < m_FileCount; ++i) {
        if (strcmp(m_FileEntries[i].name, current_image) == 0) {
            current_cd = i;
	    if (next_cd != current_cd)
	    	next_cd = i;
            break;
        }
    }

    LOGNOTE("SCSITBService::RefreshCache() RefreshCache() done");

    f_closedir(&dir);
    return true;
}

void SCSITBService::Run() {
	LOGNOTE("SCSITBService::Run started");

	while (true) {

		// Do we have a next cd?
		if (next_cd > -1) {

			// Check if it's valid
			if (next_cd > (int)m_FileCount) {
				next_cd = -1;
				continue;
			}

			// Load it
			char* imageName = m_FileEntries[next_cd].name;
			CCueBinFileDevice* cueBinFileDevice = loadCueBinFileDevice(imageName);
			
			// Set the new device in the CD gadget
    			m_pCDGadget->SetDevice(cueBinFileDevice);

			// Save current mounted image name
			m_pProperties->SelectSection("usbode");
			m_pProperties->SetString("current_image", imageName);
			m_pProperties->Save();

			current_cd = next_cd;

			// Mark done
			next_cd = -1;
		}
		CScheduler::Get()->MsSleep(100);
	}
}
