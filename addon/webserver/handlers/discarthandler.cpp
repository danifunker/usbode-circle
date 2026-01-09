#include "discarthandler.h"

#include <circle/logger.h>
#include <circle/sched/scheduler.h>
#include <scsitbservice/scsitbservice.h>
#include <discart/discart.h>
#include <cstring>

LOGMODULE("discarthandler");

THTTPStatus DiscArtHandler::GetContent(const char* pPath,
                                        const char* pParams,
                                        const char* pFormData,
                                        u8* pBuffer,
                                        unsigned* pLength,
                                        const char** ppContentType) {
    // Sanity checking
    if (!pBuffer || !pLength || !ppContentType) {
        return HTTPBadRequest;
    }

    // Get the current disc path from SCSITBService
    SCSITBService* svc = static_cast<SCSITBService*>(
        CScheduler::Get()->GetTask("scsitbservice"));

    if (!svc) {
        LOGERR("DiscArtHandler: scsitbservice is null!");
        return HTTPInternalServerError;
    }

    const char* discPath = svc->GetCurrentCDPath();
    if (!discPath || discPath[0] == '\0') {
        LOGNOTE("DiscArtHandler: No disc loaded");
        return HTTPNotFound;
    }

    // Check if disc art exists and get file size
    unsigned int fileSize = DiscArt::GetDiscArtFileSize(discPath);
    if (fileSize == 0) {
        LOGNOTE("DiscArtHandler: No disc art for: %s", discPath);
        return HTTPNotFound;
    }

    // Check if buffer is large enough
    if (*pLength < fileSize) {
        LOGERR("DiscArtHandler: Buffer too small (%u < %u)", *pLength, fileSize);
        return HTTPInternalServerError;
    }

    // Read the disc art file
    unsigned int bytesRead = DiscArt::ReadDiscArtFile(discPath, pBuffer, *pLength);
    if (bytesRead == 0) {
        LOGERR("DiscArtHandler: Failed to read disc art file");
        return HTTPInternalServerError;
    }

    *pLength = bytesRead;
    *ppContentType = "image/jpeg";

    LOGNOTE("DiscArtHandler: Served disc art (%u bytes)", bytesRead);
    return HTTPOK;
}
