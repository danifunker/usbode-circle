#ifndef WS_UTIL_H
#define WS_UTIL_H

#include <string>
#include <map>

std::string url_decode(const std::string& str);
std::map<std::string, std::string> parse_query_params(const char* pParams);

#endif // WS_UTIL_H
