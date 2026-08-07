//
// wificonfig.h
//
// Staging and commit for a Wi-Fi configuration uploaded over the SCSI
// Toolbox send-file commands (escsitoolbox `put`).
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
#ifndef _configservice_wificonfig_h
#define _configservice_wificonfig_h

#include <circle/types.h>

// Holds an uploaded wpa_supplicant.conf in RAM until a task context can write
// it. Only ProcessCommit() touches FatFs; everything else runs in IRQ context.
class CWiFiConfigUpload
{
public:
    // A WPA configuration with a handful of networks is a few hundred bytes.
    // 8 KiB is the documented cap and bounds the IRQ-side staging buffer.
    static const u32 MaxConfigSize = 8192;

    // Fixed by the protocol, not a buffer choice: CDB block indices scale by it.
    static const u32 BlockSize = 512;

    // 32 filename characters plus the terminator, matching the 33-byte
    // parameter list TOOLBOX_SEND_FILE_PREP carries.
    static const u32 NameFieldSize = 33;

    static CWiFiConfigUpload &Get(void);

    // --- IRQ context: the SCSI data-out path ---

    // pName is the raw parameter list: it must carry its own NUL and name a
    // file this upload is allowed to replace.
    bool Begin(const u8 *pName, size_t nNameLength);

    // A repeated block index overwrites in place rather than appending.
    bool Stage(u32 nBlockIndex, const u8 *pData, u32 nLength);

    // Fails when nothing is staged, so an empty upload cannot truncate the file.
    bool RequestCommit(void);

    // Discard a half-received upload. A queued or running commit is left
    // alone: it no longer depends on the host.
    void Abort(void);

    bool IsReceiving(void) const { return m_State == StateReceiving; }
    bool IsBusy(void) const
    {
        return m_State == StateCommitQueued || m_State == StateCommitting;
    }

    u32 StagedLength(void) const { return m_nLength; }
    const char *StagedName(void) const { return m_Name; }

    // --- Task context: ConfigService::Run() ---

    bool CommitPending(void) const { return m_State == StateCommitQueued; }

    // Leaves the existing configuration untouched if any step fails, and
    // clears the staged bytes either way.
    void ProcessCommit(void);

    // True once after a commit succeeded, so the caller schedules exactly one
    // reboot.
    bool ConsumeRebootRequest(void);

private:
    enum TState
    {
        StateIdle,
        StateReceiving,
        StateCommitQueued,
        StateCommitting
    };

    void Wipe(void);
    bool WriteTempFile(void);
    bool InstallTempFile(void);

    TState m_State = StateIdle;
    u32 m_nLength = 0;
    bool m_bRebootRequested = false;
    char m_Name[NameFieldSize] = {0};
    u8 m_Buffer[MaxConfigSize] = {0};
};

#endif
