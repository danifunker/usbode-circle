#ifndef UPGRADESTATUS_H
#define UPGRADESTATUS_H

#include <circle/string.h>
#include <circle/types.h>
#include <SDCard/emmc.h>
#include <fatfs/ff.h>
#include <assert.h>

class UpgradeStatus {
public:

    void Run();

    // Singleton getter
    static UpgradeStatus* Get();
    
    // Status management
    bool isUpgradeRequired() const;
    bool isUpgradeInProgress() const;
    bool isUpgradeComplete() const;
    const char* getStatusMessage() const;
    int getCurrentProgress() const;
    int getTotalProgress() const;
    bool performUpgrade();

private:
    UpgradeStatus();
    ~UpgradeStatus() = default;
    UpgradeStatus(const UpgradeStatus&) = delete;
    UpgradeStatus& operator=(const UpgradeStatus&) = delete;

    static UpgradeStatus* s_pThis;

    bool checkUpgradeExists();
    
    // Status variables
    volatile bool m_upgradeRequired = false;
    volatile bool m_upgradeInProgress = false;
    volatile bool m_upgradeComplete = false;
    volatile int m_currentProgress = 1;
    volatile int m_totalProgress = 5;
    const char*  m_statusMessage;
    const char* tarpath = "0:/sysupgrade.tar";

    // crc32
    uint32_t crc32_table[256];
    uint32_t crc32_init();
    uint32_t crc32_update(uint32_t crc, const uint8_t *buf, size_t len);
    uint32_t crc32_final(uint32_t crc);
    void init_crc32_table();

    // Tar
    struct TarHeader {
        char name[100];
        char mode[8];
        char uid[8];
        char gid[8];
        char size[12];
        char mtime[12];
        char chksum[8];
        char typeflag;
        char linkname[100];
        char magic[6];
        char version[2];
        char uname[32];
        char gname[32];
        char devmajor[8];
        char devminor[8];
        char prefix[155];
        char pad[12];
    };
    size_t tar_octal_to_size(const char *str, size_t len);
    bool extractFileFromTar(const char *tarPath, const char *wantedName, const char *destPath);
    bool extractAllFromTar(const char *tarPath, const char *destDir);
    
};

#endif // UPGRADESTATUS_H
