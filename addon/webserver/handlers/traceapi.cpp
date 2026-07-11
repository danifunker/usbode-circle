#include <circle/logger.h>
#include <circle/util.h>
#include <circle/net/httpdaemon.h>
#include <json/json.hpp>
#include <tracelab/tracelab.h>
#include <string>
#include "traceapi.h"

LOGMODULE("traceapi");

// Exported to the boot partition alongside logfile.txt so users can grab it
// by re-inserting the SD card, without needing the images partition mounted.
#define TRACE_EXPORT_PATH "0:/usbode.utrace"

THTTPStatus TraceAPIHandler::GetJson(nlohmann::json& j,
                const char *pPath,
                const char *pParams,
                const char *pFormData)
{
    if (!pPath)
        return HTTPNotFound;

    std::string path(pPath);
    CTraceLab *pTraceLab = CTraceLab::Get();
    bool enabled = pTraceLab != nullptr && pTraceLab->IsEnabled();

    if (path == "/api/trace") {
        j["enabled"] = enabled;
        if (enabled) {
            j["records"] = pTraceLab->GetRecordCount();
            j["dropped"] = pTraceLab->GetDroppedRecordCount();
            j["buffer_bytes"] = pTraceLab->GetBufferCapacity();
            j["download"] = "/usbode.utrace";
        }
        return HTTPOK;
    }

    if (path == "/api/trace/save") {
        if (!enabled) {
            j["status"] = "error";
            j["error"] = "tracing is not enabled; set trace_mode=standard in config.txt";
            return HTTPOK;
        }

        // The webserver runs in task context, which is the only place
        // CTraceLab::SaveToSD may be called from.
        if (pTraceLab->SaveToSD(TRACE_EXPORT_PATH)) {
            j["status"] = "saved";
            j["file"] = TRACE_EXPORT_PATH;
            j["records"] = pTraceLab->GetRecordCount();
            j["dropped"] = pTraceLab->GetDroppedRecordCount();
        } else {
            j["status"] = "error";
            j["error"] = "failed to write trace file";
        }
        return HTTPOK;
    }

    return HTTPNotFound;
}

THTTPStatus TraceDownloadHandler::GetContent(const char* pPath,
                                             const char* pParams,
                                             const char* pFormData,
                                             u8* pBuffer,
                                             unsigned* pLength,
                                             const char** ppContentType)
{
    if (!pBuffer || !pLength || !ppContentType) {
        return HTTPBadRequest;
    }

    CTraceLab *pTraceLab = CTraceLab::Get();
    if (pTraceLab == nullptr || !pTraceLab->IsEnabled()) {
        LOGNOTE("Trace download requested but tracing is not enabled");
        return HTTPNotFound;
    }

    u32 nLength = pTraceLab->ExportToBuffer(pBuffer, *pLength);
    if (nLength == 0) {
        LOGERR("Trace export failed or capture too large for HTTP buffer (%u bytes); use /api/trace/save",
               *pLength);
        return HTTPInternalServerError;
    }

    *pLength = nLength;
    *ppContentType = "application/octet-stream";
    return HTTPOK;
}
