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
#include <circle/string.h>
#include <scsitbservice/scsitbservice.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include "pagehandlerregistry.h"
#include "util.h"

// Large enough for a Trace Lab capture up to trace_buffer_kb=1024 (plus
// file header) to be downloaded via /usbode.utrace in a single response.
#define MAX_CONTENT_SIZE 1114112
// Multipart buffer for /api/images/upload: fits one 1 MB chunk from the
// browser-side chunked uploader plus multipart boundary overhead.
#define MAX_UPLOAD_PART_SIZE 1114112
#define MAX_FILES 1024
#define MAX_FILES_PER_PAGE 50
#define MAX_FILENAME 255
#define VERSION "2.0.1"
#define DRIVE "0:"
#define CONFIG_FILE DRIVE "/config.txt"

LOGMODULE("webserver");

CWebServer::CWebServer (CNetSubSystem *pNetSubSystem, CActLED *pActLED, CSocket *pSocket)
:       CHTTPDaemon (pNetSubSystem, pSocket, MAX_CONTENT_SIZE, HTTP_PORT, MAX_UPLOAD_PART_SIZE),
        m_pActLED (pActLED)
{
    cdromservice = static_cast<CDROMService*>(CScheduler::Get()->GetTask("cdromservice"));
    assert(cdromservice != nullptr && "Failed to get cdromservice");
}

CWebServer::~CWebServer (void)
{
        m_pActLED = 0;
}

CHTTPDaemon *CWebServer::CreateWorker (CNetSubSystem *pNetSubSystem, CSocket *pSocket)
{
        return new CWebServer (pNetSubSystem, m_pActLED, pSocket);
}

THTTPStatus CWebServer::GetContent (const char  *pPath,
                                   const char  *pParams,
                                   const char  *pFormData,
                                   u8          *pBuffer,
                                   unsigned    *pLength,
                                   const char **ppContentType)
{
    // Handled here rather than in a registry handler because the multipart
    // body is only reachable via CHTTPDaemon::GetMultipartFormPart(), which
    // is a protected member of this class.
    if (strcmp(pPath, "/api/images/upload") == 0)
        return HandleImageUpload(pParams, pBuffer, pLength, ppContentType);

    IPageHandler* handler = PageHandlerRegistry::getHandler(pPath);

    if (handler)
	    return handler->GetContent(pPath, pParams, pFormData, pBuffer, pLength, ppContentType);

    return HTTPInternalServerError;
}

static THTTPStatus UploadReply (u8 *pBuffer, unsigned *pLength,
                                const char **ppContentType, const char *pJson)
{
    unsigned n = strlen(pJson);
    if (n >= *pLength)
        return HTTPInternalServerError;
    memcpy(pBuffer, pJson, n);
    *pLength = n;
    *ppContentType = "application/json";
    return HTTPOK;
}

THTTPStatus CWebServer::HandleImageUpload (const char *pParams,
                                           u8 *pBuffer,
                                           unsigned *pLength,
                                           const char **ppContentType)
{
    auto params = parse_query_params(pParams);
    std::string name = params.count("name") ? params["name"] : "";
    unsigned long offset = params.count("offset")
        ? strtoul(params["offset"].c_str(), nullptr, 10) : 0;
    boolean bDone = params.count("done") && params["done"] == "1";

    // Uploads land in the root of the images volume; reject anything that
    // could escape it or collide with in-progress upload temp files.
    if (name.empty() || name.length() > 200 || name[0] == '.'
        || name.find('/') != std::string::npos
        || name.find('\\') != std::string::npos
        || name.find("..") != std::string::npos)
        return UploadReply(pBuffer, pLength, ppContentType,
                           "{\"status\":\"error\",\"error\":\"invalid file name\"}");

    std::string partPath = "1:/" + name + ".part";
    std::string finalPath = "1:/" + name;

    const char *pHeader;
    const u8 *pData;
    unsigned nDataLength = 0;
    if (!GetMultipartFormPart(&pHeader, &pData, &nDataLength))
    {
        pData = nullptr;
        nDataLength = 0; // final rename-only request or empty file
    }

    FIL file;
    FRESULT res;
    if (offset == 0)
    {
        res = f_open(&file, partPath.c_str(), FA_WRITE | FA_CREATE_ALWAYS);
    }
    else
    {
        res = f_open(&file, partPath.c_str(), FA_WRITE | FA_OPEN_EXISTING);
        if (res == FR_OK && f_size(&file) != offset)
        {
            unsigned long haveSize = (unsigned long)f_size(&file);
            f_close(&file);
            if (haveSize == offset + nDataLength && !bDone)
            {
                // Retry of a chunk that was already written but whose
                // response got lost - acknowledge it instead of failing.
                char reply[96];
                snprintf(reply, sizeof(reply),
                         "{\"status\":\"ok\",\"received\":%u,\"size\":%lu}",
                         nDataLength, haveSize);
                return UploadReply(pBuffer, pLength, ppContentType, reply);
            }
            // Chunk out of sequence; the uploader must restart.
            LOGERR("Upload %s: offset %lu != file size %lu",
                   name.c_str(), offset, haveSize);
            return UploadReply(pBuffer, pLength, ppContentType,
                               "{\"status\":\"error\",\"error\":\"chunk out of sequence, restart upload\"}");
        }
        if (res == FR_OK)
            res = f_lseek(&file, offset);
    }
    if (res != FR_OK)
        return UploadReply(pBuffer, pLength, ppContentType,
                           "{\"status\":\"error\",\"error\":\"cannot open file on images volume\"}");

    if (nDataLength > 0)
    {
        UINT written = 0;
        res = f_write(&file, pData, nDataLength, &written);
        if (res != FR_OK || written != nDataLength)
        {
            f_close(&file);
            f_unlink(partPath.c_str());
            LOGERR("Upload %s: write failed at offset %lu (res=%d, wrote %u/%u)",
                   name.c_str(), offset, (int)res, written, nDataLength);
            return UploadReply(pBuffer, pLength, ppContentType,
                               "{\"status\":\"error\",\"error\":\"write failed (card full?)\"}");
        }
    }
    f_close(&file);

    if (bDone)
    {
        SCSITBService* svc = static_cast<SCSITBService*>(
            CScheduler::Get()->GetTask("scsitbservice"));

        const char *current = svc ? svc->GetCurrentCDPath() : nullptr;
        if (current && finalPath == current)
        {
            f_unlink(partPath.c_str());
            return UploadReply(pBuffer, pLength, ppContentType,
                               "{\"status\":\"error\",\"error\":\"cannot replace the mounted image\"}");
        }

        f_unlink(finalPath.c_str()); // ignore result; may not exist
        res = f_rename(partPath.c_str(), finalPath.c_str());
        if (res != FR_OK)
        {
            LOGERR("Upload %s: rename failed (res=%d)", name.c_str(), (int)res);
            return UploadReply(pBuffer, pLength, ppContentType,
                               "{\"status\":\"error\",\"error\":\"rename failed\"}");
        }

        LOGNOTE("Upload complete: %s (%lu bytes)",
                finalPath.c_str(), offset + nDataLength);
        if (svc)
            svc->RefreshCache();
    }

    char reply[96];
    snprintf(reply, sizeof(reply),
             "{\"status\":\"ok\",\"received\":%u,\"size\":%lu}",
             nDataLength, offset + nDataLength);
    return UploadReply(pBuffer, pLength, ppContentType, reply);
}

