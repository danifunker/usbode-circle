#include "upgradestatus.h"
#include <circle/logger.h>
#include <circle/sched/scheduler.h>
#include <circle/timer.h>
#include <circle/util.h>
#include <gitinfo/gitinfo.h>

LOGMODULE("upgradestatus");

UpgradeStatus* UpgradeStatus::s_pThis = nullptr;

UpgradeStatus::UpgradeStatus()
    : m_upgradeRequired(false),
      m_upgradeInProgress(false), 
      m_upgradeComplete(false),
      m_currentProgress(0),
      m_totalProgress(0),
      m_statusMessage("Upgrade starting...")
{
    m_pTransferBuffer = new uint8_t[BUFFER_SIZE];
    
    LOGNOTE("UpgradeStatus service initialized");
    
    // Check if upgrade is needed
    if (checkUpgradeExists()) {
        m_upgradeRequired = true;
    }
}

UpgradeStatus::~UpgradeStatus() {
    delete[] m_pTransferBuffer;
    m_pTransferBuffer = nullptr;
}

UpgradeStatus* UpgradeStatus::Get() {
    if (s_pThis == nullptr)
        s_pThis = new UpgradeStatus();
    return s_pThis;
}
    
bool UpgradeStatus::isUpgradeInProgress() const {
    return m_upgradeInProgress;
}

bool UpgradeStatus::isUpgradeComplete() const {
    return m_upgradeComplete;
}

bool UpgradeStatus::isUpgradeRequired() const {
    return m_upgradeRequired;
}

const char* UpgradeStatus::getStatusMessage() const {
    return m_statusMessage;
}

int UpgradeStatus::getCurrentProgress() const {
    return m_currentProgress;
}

int UpgradeStatus::getTotalProgress() const {
    return m_totalProgress;
}

bool UpgradeStatus::checkUpgradeExists() {
    FILINFO fno;
    if (f_stat(tarpath, &fno) != FR_OK) {
        LOGNOTE("Upgrade not found");
        return false;
    }
    LOGNOTE("Upgrade found");
    return true;
}

uint32_t UpgradeStatus::crc32_init() {
    return 0xFFFFFFFF;
}

void UpgradeStatus::init_crc32_table() {
    uint32_t poly = 0xEDB88320;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (uint32_t j = 0; j < 8; j++) {
            if (c & 1)
                c = poly ^ (c >> 1);
            else
                c >>= 1;
        }
        crc32_table[i] = c;
    }
}

uint32_t UpgradeStatus::crc32_update(uint32_t crc, const uint8_t *buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc;
}

uint32_t UpgradeStatus::crc32_final(uint32_t crc) {
    return crc ^ 0xFFFFFFFF;
}

size_t UpgradeStatus::tar_octal_to_size(const char *str, size_t len) {
    char temp_str[13]; // 12 bytes for size, 1 for null terminator
    strncpy(temp_str, str, len);
    temp_str[len] = '\0';
    return (size_t)strtoul(temp_str, nullptr, 8);
}

bool UpgradeStatus::extractFileFromTar(const char *tarPath, const char *wantedName, const char *destPath) {
    FIL tarFile, outFile;
    if (f_open(&tarFile, tarPath, FA_READ) != FR_OK)
        return false;

    UINT br, bw;
    uint8_t header[512];
    UINT czero = 0;

    while (1) {

        // Let the web interface update
        CScheduler::Get()->Yield();

        // Read the next 512-byte header block
        if (f_read(&tarFile, header, 512, &br) != FR_OK || br != 512)
            break;

        // Check for end-of-archive: two consecutive zero blocks
        bool allZero = true;
        for (int i = 0; i < 512; i++) {
            if (header[i] != 0) { 
               allZero = false; 
               czero = 0;
               break; 
            }
        }

        // 2 consecutive zero block is EOA
        if (allZero && ++czero > 1) {
            LOGNOTE("End of archive");
            break;
        }

        // Go read another block
        if (allZero)
            continue;

        TarHeader *h = (TarHeader*)header;
        size_t filesize = tar_octal_to_size(h->size, sizeof(h->size));

        LOGNOTE("Found file in tar: '%s'", h->name);
        LOGNOTE("Filesize is %u", filesize);

        if (strcmp(h->name, wantedName) == 0) {
            // Open output file
            if (f_open(&outFile, destPath, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) {
                f_close(&tarFile);
		LOGNOTE("Can't open output file %s", destPath);
                return false;
            }

            // Copy file contents
            size_t remaining = filesize;
            while (remaining > 0) {

        	// Let the web interface update
	        CScheduler::Get()->Yield();

                size_t chunk = (remaining > BUFFER_SIZE) ? BUFFER_SIZE : remaining;
                if (f_read(&tarFile, m_pTransferBuffer, chunk, &br) != FR_OK || br != chunk) {
                    f_close(&outFile);
                    f_close(&tarFile);
                    LOGNOTE("Error reading tar file");
                    return false;
                }

	        CScheduler::Get()->Yield();
                if (f_write(&outFile, m_pTransferBuffer, chunk, &bw) != FR_OK || bw != chunk) {
                    f_close(&outFile);
                    f_close(&tarFile);
                    LOGNOTE("Error writing tar file");
                    return false;
                }
                remaining -= chunk;
            }
            f_close(&outFile);
            f_close(&tarFile);
            LOGNOTE("Extracted %s", wantedName);
            return true;
        } else {
            // Skip this file's content + padding
            DWORD skip = ((filesize + 511) / 512) * 512; 
            LOGNOTE("Skipping %lu bytes", skip);
            f_lseek(&tarFile, f_tell(&tarFile) + skip);
        }
    }

    f_close(&tarFile);
    LOGNOTE("End of file");
    return false;
}

bool UpgradeStatus::extractAllFromTar(const char *tarPath, const char *destDir) {
    FIL tarFile, outFile;
    if (f_open(&tarFile, tarPath, FA_READ) != FR_OK) {
        LOGNOTE("Could not open tar file %s", tarPath);
        return false;
    }

    UINT br, bw;
    uint8_t header[512];
    char fullPath[256];
    UINT czero = 0;

    while (1) {

        // Let the web interface update
        CScheduler::Get()->Yield();

        if (f_read(&tarFile, header, 512, &br) != FR_OK || br != 512) {
            LOGNOTE("Could not read tar file");
            f_close(&tarFile);
            return false;
        }

        // Check for end-of-archive: two consecutive zero blocks
        bool allZero = true;
        for (int i = 0; i < 512; i++) {
            if (header[i] != 0) { 
               allZero = false; 
               czero = 0;
               break; 
            }
        }

        // 2 consecutive zero block is EOA
        if (allZero && ++czero > 1) {
            LOGNOTE("End of archive");
            break;
        }

        // Go read another block
        if (allZero)
            continue;

        TarHeader *h = (TarHeader*)header;
        size_t filesize = tar_octal_to_size(h->size, sizeof(h->size));

	// Check if the file name starts with "./" and adjust the pointer if necessary
	const char *fileName = h->name;
	if (strncmp(fileName, "./", 2) == 0) {
	    fileName += 2; // Move the pointer past the "./"
	}

        // Build destination path
        strcpy(fullPath, destDir); 
        strcat(fullPath, fileName);

        if (h->typeflag == '0' || h->typeflag == 0) {
            LOGNOTE("Extracting regular file %s", fullPath);
            // --- Regular file ---
            if (f_open(&outFile, fullPath, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) {
                f_close(&tarFile);
		LOGNOTE("Can't open output file %s", fullPath);
                return false;
            }

            size_t remaining = filesize;
            while (remaining > 0) {
                CScheduler::Get()->Yield();
                size_t chunk = (remaining > BUFFER_SIZE) ? BUFFER_SIZE : remaining;
                if (f_read(&tarFile, m_pTransferBuffer, chunk, &br) != FR_OK || br != chunk) {
                    f_close(&outFile);
                    f_close(&tarFile);
		    LOGNOTE("Can't read tar file");
                    return false;
                }
                CScheduler::Get()->Yield();
                if (f_write(&outFile, m_pTransferBuffer, chunk, &bw) != FR_OK || bw != chunk) {
                    f_close(&outFile);
                    f_close(&tarFile);
		    LOGNOTE("Can't write output file");
                    return false;
                }
                remaining -= chunk;
            }
            f_close(&outFile);

            // Skip padding to 512-byte boundary
            DWORD skip = ((filesize + 511) / 512) * 512 - filesize;
            if (skip > 0) {
                if (f_lseek(&tarFile, f_tell(&tarFile) + skip) != FR_OK) {
                    LOGNOTE("Can't seek");
                    f_close(&tarFile);
                    return false;
                }
            }
        } else if (h->typeflag == '5') {

	    // Check for an empty or root directory name
	    if (strcmp(fullPath, destDir) == 0) {
		LOGNOTE("Skipping root directory entry: %s", fullPath);
		continue;
	    }

            LOGNOTE("Creating directory %s", fullPath);
	    
	    // Otherwise, attempt to create the directory
	    FRESULT res = f_mkdir(fullPath);
	    if (!(res == FR_OK || res == FR_EXIST)) {
		f_close(&tarFile);
		LOGNOTE("Can't create directory %s, %d", fullPath, res);
		return false;
	    }

        } else {
            LOGNOTE("Skipping unsupported file %s", fullPath);
            // --- Other types (links, devices) not supported, just skip content ---
            DWORD skip = ((filesize + 511) / 512) * 512;
            if (f_lseek(&tarFile, f_tell(&tarFile) + skip) != FR_OK) {
                f_close(&tarFile);
                LOGNOTE("Can't see to skip content");
                return false;
            }
        }
    }

    f_close(&tarFile);
    LOGNOTE("Done");
    return true;
}

bool UpgradeStatus::performUpgrade() {

    // double check if we actually need to run
    if (!m_upgradeRequired)
        return false;

    LOGNOTE("Starting upgrade process...");

    m_upgradeInProgress = true;

    const char* archtype = CGitInfo::Get()->GetArchBits();

    char tarballName[32];
    char crcName[32];
    char extractedTar[32] = "0:/upgrade.tar";
    char extractedCrc[32] = "0:/upgrade.crc";

    // Build filenames like "upgrade32.tar", "upgrade64.crc"
    strcpy(tarballName, "upgrade");
    strcat(tarballName, archtype);
    strcat(tarballName, ".tar");

    strcpy(crcName, "upgrade");
    strcat(crcName, archtype);
    strcat(crcName, ".crc");

    LOGNOTE("Extracting upgrade");
    m_statusMessage = "Extracting upgrade";
    m_currentProgress = 1;
    CScheduler::Get()->Yield();
    if (!extractFileFromTar("0:/sysupgrade.tar", tarballName, extractedTar))  {
        LOGERR("Can't find %s within sysupgrade.tar, aborting", tarballName);
        f_unlink("0:/sysupgrade.tar");
        return false;
    }
    LOGNOTE("Extracted upgrade file %s", tarballName);

    LOGNOTE("Extracting crc");
    m_statusMessage = "Extracting checksum";
    m_currentProgress = 2;
    CScheduler::Get()->Yield();
    if (!extractFileFromTar("0:/sysupgrade.tar", crcName, extractedCrc)) {
        LOGERR("Can't find %s within sysupgrade.tar, aborting", crcName);
        f_unlink(extractedTar);
        f_unlink("0:/sysupgrade.tar");
        return false;
    }
    LOGNOTE("Extracted crc file %s", crcName);

    // Read expected CRC
    FIL crcFile;
    UINT br;
    char buf[16];
    if (f_open(&crcFile, extractedCrc, FA_READ) != FR_OK) {
        f_unlink(extractedTar);
        f_unlink(extractedCrc);
        f_unlink("0:/sysupgrade.tar");
        LOGERR("Can't open the crc file that we've just extracted");
        return false;
    }

    if (f_read(&crcFile, buf, sizeof(buf)-1, &br) != FR_OK) { 
        LOGERR("Can't read the crc file that we've just extracted");
        f_close(&crcFile); 
        f_unlink(extractedTar);
        f_unlink(extractedCrc);
        f_unlink("0:/sysupgrade.tar");
        return false; 
    }
    f_close(&crcFile);

    buf[br] = 0;
    uint32_t expectedCrc = (uint32_t)strtoul(buf, 0, 16); // assumes CRC is stored as hex string
    LOGNOTE("Expected crc is %d", expectedCrc);

    // Compute CRC of extracted tarball
    m_statusMessage = "Validating checksum";
    m_currentProgress = 3;
    CScheduler::Get()->Yield();
    FIL tarFile;
    if (f_open(&tarFile, extractedTar, FA_READ) != FR_OK) {
        LOGERR("Can't open the tarball that we've just extracted");
        f_unlink(extractedTar);
        f_unlink(extractedCrc);
        f_unlink("0:/sysupgrade.tar");
        return false;
    }

    uint32_t crc = crc32_init();
    init_crc32_table();

    while (1) {

        // Let the web interface update
        CScheduler::Get()->Yield();

        if (f_read(&tarFile, m_pTransferBuffer, BUFFER_SIZE, &br) != FR_OK) {
            LOGERR("Can't read the tarball that we've just extracted");
            f_close(&tarFile); 
            f_unlink(extractedTar);
            f_unlink(extractedCrc);
            f_unlink("0:/sysupgrade.tar");
            return false; 
        }

        // eof
        if (br == 0) 
            break;

        crc = crc32_update(crc, m_pTransferBuffer, br);
    }
    crc = crc32_final(crc);
    f_close(&tarFile);
    LOGNOTE("Calculated crc %d", crc);

    if (crc != expectedCrc) {
        // CRC mismatch!
        LOGERR("CRC %d is not as expected %d", crc, expectedCrc);
        f_unlink(extractedTar);
        f_unlink(extractedCrc);
        f_unlink("0:/sysupgrade.tar");
        return false;
    }

    // Now extract the chosen upgrade tar to "0:/"
    m_statusMessage = "Unpacking files";
    m_currentProgress = 4;
    CScheduler::Get()->Yield();
    if (!extractAllFromTar(extractedTar, "0:/")) {
        LOGERR("Could not extract all files from %s", extractedTar);
        f_unlink(extractedTar);
        f_unlink(extractedCrc);
        f_unlink("0:/sysupgrade.tar");
        return false;
    }

    // Final cleanup
    f_unlink(extractedTar);
    f_unlink(extractedCrc);
    f_unlink("0:/sysupgrade.tar");

    LOGNOTE("Finished");

    m_statusMessage = "Finished, rebooting";
    m_currentProgress = 5;
    CScheduler::Get()->Yield();

    return true;
}
