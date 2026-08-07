//
// wificonfig.cpp
//
// Copyright (C) 2025 USBODE contributors
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
#include <configservice/wificonfig.h>

#include <circle/logger.h>
#include <circle/util.h>
#include <fatfs/ff.h>

LOGMODULE("wificonfig");

#define WIFI_CONFIG_PATH   "0:/wpa_supplicant.conf"
#define WIFI_CONFIG_TEMP   "0:/wpa_supplicant.tmp"
#define WIFI_CONFIG_BACKUP "0:/wpa_supplicant.bak"

// File scope rather than function-local: Get() is reached from IRQ context,
// where taking a static initialization guard is not something to rely on.
static CWiFiConfigUpload s_WiFiConfigUpload;

CWiFiConfigUpload &CWiFiConfigUpload::Get(void)
{
    return s_WiFiConfigUpload;
}

// A plain memset over a buffer nothing reads again is dead-store eliminated,
// which would leave the password in RAM. Writing through volatile cannot be.
static void SecureZero(void *pBuffer, size_t nSize)
{
    volatile u8 *p = (volatile u8 *)pBuffer;
    while (nSize-- > 0)
    {
        *p++ = 0;
    }
}

// This is a Wi-Fi configuration upload, not a general file transfer. Only the
// two names USBODE documents for that file are accepted.
static const char *const s_AcceptedNames[] = {"WIFI.CFG", "wpa_supplicant.conf"};

static char ToLower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static bool EqualsIgnoreCase(const char *pA, const char *pB)
{
    while (*pA != '\0' && *pB != '\0')
    {
        if (ToLower(*pA++) != ToLower(*pB++))
        {
            return false;
        }
    }
    return *pA == '\0' && *pB == '\0';
}

bool CWiFiConfigUpload::Begin(const u8 *pName, size_t nNameLength)
{
    if (IsBusy() || pName == nullptr)
    {
        return false;
    }

    // Staging dies on a second PREP even if this one is about to be rejected.
    Abort();

    if (nNameLength > NameFieldSize)
    {
        nNameLength = NameFieldSize;
    }

    size_t nLength = 0;
    while (nLength < nNameLength && pName[nLength] != '\0')
    {
        nLength++;
    }
    if (nLength == 0 || nLength == nNameLength)
    {
        return false; // empty, or no terminator inside the parameter list
    }

    for (size_t i = 0; i < nLength; i++)
    {
        u8 c = pName[i];
        if (c < 0x20 || c == 0x7F)
        {
            return false; // embedded control characters
        }
        if (c == '/' || c == '\\' || c == ':')
        {
            return false; // path separators and drive prefixes
        }
    }
    if (nLength >= 2 && pName[0] == '.' && pName[1] == '.')
    {
        return false; // dot traversal
    }

    char name[NameFieldSize];
    memcpy(name, pName, nLength);
    name[nLength] = '\0';

    bool bAccepted = false;
    for (size_t i = 0; i < sizeof(s_AcceptedNames) / sizeof(s_AcceptedNames[0]); i++)
    {
        if (EqualsIgnoreCase(name, s_AcceptedNames[i]))
        {
            bAccepted = true;
            break;
        }
    }
    if (!bAccepted)
    {
        return false;
    }

    memset(m_Buffer, 0, sizeof(m_Buffer));
    m_nLength = 0;
    memcpy(m_Name, name, nLength + 1);
    m_State = StateReceiving;

    LOGNOTE("Wi-Fi configuration upload started (%s)", m_Name);
    return true;
}

bool CWiFiConfigUpload::Stage(u32 nBlockIndex, const u8 *pData, u32 nLength)
{
    if (m_State != StateReceiving || pData == nullptr)
    {
        return false;
    }
    if (nLength == 0 || nLength > BlockSize)
    {
        return false;
    }
    if (nBlockIndex > (MaxConfigSize - 1) / BlockSize)
    {
        return false;
    }

    // nBlockIndex is bounded above, so the product fits; the subtraction cannot
    // wrap because nLength is at most BlockSize.
    u32 nOffset = nBlockIndex * BlockSize;
    if (nOffset > MaxConfigSize - nLength)
    {
        return false;
    }

    // Refusing a gap keeps the staged file contiguous. Retries land at or
    // inside what is already there, so they still pass.
    if (nOffset > m_nLength)
    {
        return false;
    }

    memcpy(m_Buffer + nOffset, pData, nLength);
    if (nOffset + nLength > m_nLength)
    {
        m_nLength = nOffset + nLength;
    }
    return true;
}

bool CWiFiConfigUpload::RequestCommit(void)
{
    if (m_State != StateReceiving || m_nLength == 0)
    {
        return false;
    }
    m_State = StateCommitQueued;
    return true;
}

void CWiFiConfigUpload::Abort(void)
{
    if (IsBusy())
    {
        return;
    }
    Wipe();
}

void CWiFiConfigUpload::Wipe(void)
{
    SecureZero(m_Buffer, sizeof(m_Buffer));
    SecureZero(m_Name, sizeof(m_Name));
    m_nLength = 0;
    m_State = StateIdle;
}

void CWiFiConfigUpload::ProcessCommit(void)
{
    if (m_State != StateCommitQueued)
    {
        return;
    }

    // Claimed before the first FatFs call, which is the first place this can
    // yield, so the IRQ side cannot wipe the buffer from under the write.
    m_State = StateCommitting;

    u32 nLength = m_nLength;
    if (WriteTempFile() && InstallTempFile())
    {
        LOGNOTE("Wi-Fi configuration replaced (%u bytes)", (unsigned)nLength);
        m_bRebootRequested = true;
    }
    else
    {
        LOGERR("Wi-Fi configuration upload failed, no new configuration installed");
    }

    Wipe();
}

bool CWiFiConfigUpload::ConsumeRebootRequest(void)
{
    if (!m_bRebootRequested)
    {
        return false;
    }
    m_bRebootRequested = false;
    return true;
}

bool CWiFiConfigUpload::WriteTempFile(void)
{
    f_unlink(WIFI_CONFIG_TEMP);

    FIL File;
    if (f_open(&File, WIFI_CONFIG_TEMP, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
    {
        return false;
    }

    UINT nWritten = 0;
    FRESULT Result = f_write(&File, m_Buffer, (UINT)m_nLength, &nWritten);

    // A full card is FR_OK with a short count, so the byte count decides, not
    // the result code.
    if (Result != FR_OK || nWritten != m_nLength || f_sync(&File) != FR_OK)
    {
        f_close(&File);
        f_unlink(WIFI_CONFIG_TEMP);
        return false;
    }

    if (f_close(&File) != FR_OK)
    {
        f_unlink(WIFI_CONFIG_TEMP);
        return false;
    }
    return true;
}

static bool FileExists(const char *pPath)
{
    FIL File;
    if (f_open(&File, pPath, FA_READ) != FR_OK)
    {
        return false;
    }
    f_close(&File);
    return true;
}

bool CWiFiConfigUpload::InstallTempFile(void)
{
    // A backup with no working file beside it is a configuration rescued from
    // a failed rollback, so stale backups are only cleared when one exists.
    bool bHaveWorking = FileExists(WIFI_CONFIG_PATH);
    if (bHaveWorking)
    {
        f_unlink(WIFI_CONFIG_BACKUP);
    }

    // FAT has no atomic replace, so the working file is moved aside first and
    // moved back if the new one cannot take its place.
    FRESULT Moved = FR_NO_FILE;
    if (bHaveWorking)
    {
        Moved = f_rename(WIFI_CONFIG_PATH, WIFI_CONFIG_BACKUP);
        if (Moved != FR_OK)
        {
            f_unlink(WIFI_CONFIG_TEMP);
            return false;
        }
    }

    if (f_rename(WIFI_CONFIG_TEMP, WIFI_CONFIG_PATH) != FR_OK)
    {
        // If the rollback fails too the old configuration still exists, just
        // under the backup name, so say where it is rather than claim success.
        if (Moved == FR_OK && f_rename(WIFI_CONFIG_BACKUP, WIFI_CONFIG_PATH) != FR_OK)
        {
            LOGERR("Could not restore " WIFI_CONFIG_PATH ", it is now " WIFI_CONFIG_BACKUP);
        }
        f_unlink(WIFI_CONFIG_TEMP);
        return false;
    }

    f_unlink(WIFI_CONFIG_BACKUP);
    return true;
}
