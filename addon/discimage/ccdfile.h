#ifndef _CCDFILEDEVICE_H
#define _CCDFILEDEVICE_H

#include "cuebinfile.h"
#include <ccdparser/ccdparser.h>

/// CloneCD (CCD/IMG/SUB) image support.
///
/// The .ccd TOC is translated into a cue sheet (file-relative INDEX
/// positions from the [TRACK] sections, REM SESSION markers from the
/// [Entry] sessions) and everything else - virtual byte space, session
/// gaps, normalized cue emission - is inherited from CCueBinFileDevice.
/// The optional .sub file adds stored subchannel data: 96 bytes per img
/// sector, stored LINEAR (P then Q ... W) and interleaved on the way out.
class CCCDFileDevice : public CCueBinFileDevice {
   public:
    /// ccdPath locates the .img/.sub files; ccd_str is the .ccd text.
    CCCDFileDevice(const char* ccdPath, const char* ccd_str, MEDIA_TYPE mediaType = MEDIA_TYPE::CD);
    ~CCCDFileDevice(void);

    bool Init(); // shadows CCueBinFileDevice::Init, which it calls

    FileType GetFileType() const override { return FileType::CCD; }

    bool HasSubchannelData() const override { return m_pSubFile != nullptr; }
    int ReadSubchannel(u32 lba, u8* subchannel) override;

   private:
    bool BuildSourceCue();

    char m_CcdPath[512];
    char* m_CcdText = nullptr;
    CCDParser* m_Parser = nullptr;
    FIL* m_pSubFile = nullptr;
};

#endif
