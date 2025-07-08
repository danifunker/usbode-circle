//
// webserver.cpp
//
// Circle - A C++ bare metal environment for Raspberry Pi
// Copyright (C) 2015  R. Stange <rsta2@o2online.de>
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
#include "webserver.h"
#include <gitinfo/gitinfo.h>

#include <assert.h>
#include <circle/logger.h>
#include <circle/new.h>
#include <circle/string.h>
#include "pagehandlerregistry.h"

#define MAX_CONTENT_SIZE 32768
#define MAX_FILES 1024
#define MAX_FILES_PER_PAGE 50
#define MAX_FILENAME 255
#define VERSION "2.0.1"
#define DRIVE "SD:"
#define CONFIG_FILE DRIVE "/config.txt"

LOGMODULE("webserver");

CWebServer::CWebServer (CNetSubSystem *pNetSubSystem, CActLED *pActLED, CPropertiesFatFsFile *pProperties, CSocket *pSocket)
:       CHTTPDaemon (pNetSubSystem, pSocket, MAX_CONTENT_SIZE),
        m_pActLED (pActLED),
        m_pProperties(pProperties)
{
    // Select the correct section for all property operations
    m_pProperties->SelectSection("usbode");

    cdromservice = static_cast<CDROMService*>(CScheduler::Get()->GetTask("cdromservice"));
    assert(cdromservice != nullptr && "Failed to get cdromservice");
}

CWebServer::~CWebServer (void)
{
        m_pActLED = 0;
}

CHTTPDaemon *CWebServer::CreateWorker (CNetSubSystem *pNetSubSystem, CSocket *pSocket)
{
        return new CWebServer (pNetSubSystem, m_pActLED, m_pProperties, pSocket);
}

THTTPStatus CWebServer::GetContent (const char  *pPath,
                                   const char  *pParams,
                                   const char  *pFormData,
                                   u8          *pBuffer,
                                   unsigned    *pLength,
                                   const char **ppContentType)
{
    IPageHandler* handler = PageHandlerRegistry::getHandler(pPath);

    if (handler)
	    return handler->GetContent(pPath, pParams, pFormData, pBuffer, pLength, ppContentType, m_pProperties);

    return HTTPInternalServerError;
}

