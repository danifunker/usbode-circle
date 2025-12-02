#include "webglobals.h"
#include <circle/logger.h>
#include <circle/bcmrandom.h>
#include <circle/util.h>
#include <fatfs/ff.h>
#include <cstring>

LOGMODULE("webglobals");

CWebGlobals *CWebGlobals::s_pThis = nullptr;

CWebGlobals::CWebGlobals (void)
:   m_nBootID (0),
    m_bInitialized (FALSE)
{
    s_pThis = this;
}

CWebGlobals::~CWebGlobals (void)
{
    s_pThis = nullptr;
}

CWebGlobals *CWebGlobals::Get(void)
{
    if (!s_pThis)
    {
        s_pThis = new CWebGlobals();
    }
    return s_pThis;
}

void CWebGlobals::Initialize(void)
{
    if (m_bInitialized)
    {
        return;
    }

    // Generate Random Boot ID
    CBcmRandomNumberGenerator Rng;
    m_nBootID = Rng.GetNumber();
    LOGNOTE ("Boot ID: %u", m_nBootID);

    // Scan 0:/themes
    DIR Dir;
    FILINFO Fno;
    FRESULT Res = f_opendir(&Dir, "0:/themes");

    if (Res == FR_OK)
    {
        while (true)
        {
            Res = f_readdir (&Dir, &Fno);
            if (Res != FR_OK || Fno.fname[0] == 0)
            {
                break; 
            }

            if ((Fno.fattrib & AM_DIR) && 
                strcmp (Fno.fname, ".") != 0 && 
                strcmp (Fno.fname, "..") != 0)
            {
                m_Themes.push_back (std::string (Fno.fname));
                LOGNOTE ("Found theme: %s", Fno.fname);
            }
        }
        f_closedir (&Dir);
    }
    
    m_bInitialized = TRUE;
}

u32 CWebGlobals::GetBootID(void) const
{
    return m_nBootID;
}

const std::vector<std::string>& CWebGlobals::GetThemes(void) const
{
    return m_Themes;
}