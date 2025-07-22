#ifndef BASEDISPLAY_H
#define BASEDISPLAY_H

#include "idisplay.h"

class BaseDisplay : public IDisplay {
   public:
    virtual ~BaseDisplay() = default;

    // All methods are virtual, not implemented yet — override in derived class or later in this base class.
    virtual bool Initialize() override;
    virtual void Clear() override;
    virtual void ShowStatusScreen(const char* pTitle, const char* pIPAddress,
                                  const char* pISOName, const char* pUSBSpeed) override;
    virtual void ShowFileSelectionScreen(const char* pCurrentISOName, const char* pSelectedFileName,
                                         unsigned CurrentFileIndex, unsigned TotalFiles) override;
    virtual void ShowAdvancedScreen() override;
    virtual void ShowBuildInfoScreen(const char* pVersionInfo, const char* pBuildDate,
                                     const char* pGitBranch, const char* pGitCommit,
                                     const char* pBuildNumber) override;
    virtual void ShowShutdownScreen() override;
    virtual void Sleep() override;
    virtual void Wake() override;
    virtual void Refresh() override;
};

#endif  // BASEDISPLAY_H
