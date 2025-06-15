#include <circle/logger.h>
#include <circle/util.h>
#include <circle/net/httpdaemon.h>
#include <fatfs/ff.h>
#include <vector>
#include <string>
#include <cstring>
#include <map>
#include <sstream>
#include <iostream>
#include <algorithm>
#include "asset.h"
#include "util.h"

#include "logo.h"

LOGMODULE("assethandler");

THTTPStatus AssetHandler::GetContent (const char  *pPath,
                                   const char  *pParams,
                                   const char  *pFormData,
                                   u8          *pBuffer,
                                   unsigned    *pLength,
                                   const char **ppContentType,
				   CPropertiesFatFsFile *m_pProperties,
				   CUSBCDGadget *m_pCDGadget)
{
	LOGNOTE("Asset Handler called");

	auto params = parse_query_params(pParams);

	if (params.count("name") == 0)
		return HTTPBadRequest;

	std::string file_name = params["name"];

	LOGNOTE("Got filename %s from parameter", file_name.c_str());

	// Add known files here
	if (file_name == "logo.jpg") {
    		if (!pBuffer || *pLength < logo_jpg_len)
      		  return HTTPInternalServerError;

		std::memcpy(pBuffer, logo_jpg, logo_jpg_len);
		*ppContentType = "image/jpg";
		*pLength = logo_jpg_len;
		return HTTPOK;
	}
	else
		return HTTPNotFound;
}
