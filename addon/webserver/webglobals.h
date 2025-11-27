#ifndef WEBGLOBALS_H
#define WEBGLOBALS_H

#include <string>
#include <vector>
#include <circle/types.h>

class CWebGlobals
{
public:
    static CWebGlobals *Get(void);
    void Initialize (void);
    u32 GetBootID (void) const;
    const std::vector<std::string>& GetThemes(void) const;

private:
    CWebGlobals(void);
    ~CWebGlobals(void);

private:
    static CWebGlobals *s_pThis;

    u32 m_nBootID;
    std::vector<std::string> m_Themes;
    boolean m_bInitialized;
};

#endif