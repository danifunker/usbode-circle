#include <circle/logger.h>
#include <circle/util.h>
#include <circle/net/httpdaemon.h>
#include <json/json.hpp>
#include <tracelab/tracelab.h>
#include <string>
#include <cstring>
#include <map>
#include "traceapi.h"
#include "../util.h"

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
    if (pTraceLab == nullptr) {
        j["status"] = "error";
        j["error"] = "trace lab not present";
        return HTTPOK;
    }

    if (path == "/api/trace") {
        j["capturing"] = (bool)pTraceLab->IsCapturing();
        j["mode"] = pTraceLab->IsDeepMode() ? "deep" : "standard";
        j["records"] = pTraceLab->GetRecordCount();
        j["dropped"] = pTraceLab->GetDroppedRecordCount();
        j["buffer_bytes"] = pTraceLab->GetBufferCapacity();
        j["used_bytes"] = pTraceLab->GetUsedBytes();
        j["trigger_armed"] = (bool)pTraceLab->IsErrorTriggerArmed();
        j["trigger_fired"] = (bool)pTraceLab->HasTriggerFired();
        if (pTraceLab->HasCapture()) {
            j["download"] = "/usbode.utrace";
        }
        return HTTPOK;
    }

    if (path == "/api/trace/start") {
        // /api/trace/start?mode=deep&trigger=error
        auto params = parse_query_params(pParams);
        bool deep = params.count("mode") && params["mode"] == "deep";
        bool trigger = params.count("trigger") && params["trigger"] == "error";

        if (pTraceLab->StartCapture(deep, trigger)) {
            j["status"] = "capturing";
            j["mode"] = deep ? "deep" : "standard";
            j["trigger_armed"] = trigger;
        } else {
            j["status"] = "error";
            j["error"] = "failed to allocate trace buffer";
        }
        return HTTPOK;
    }

    if (path == "/api/trace/stop") {
        pTraceLab->StopCapture();
        j["status"] = "stopped";
        j["records"] = pTraceLab->GetRecordCount();
        j["dropped"] = pTraceLab->GetDroppedRecordCount();
        j["download"] = "/usbode.utrace";
        return HTTPOK;
    }

    if (path == "/api/trace/save") {
        if (!pTraceLab->HasCapture()) {
            j["status"] = "error";
            j["error"] = "no capture in buffer; start one at /api/trace/start";
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
    if (pTraceLab == nullptr || !pTraceLab->HasCapture()) {
        LOGNOTE("Trace download requested but no capture in buffer");
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
