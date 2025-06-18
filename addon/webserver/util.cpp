#include <string>
#include <sstream>
#include <map>
#include <cstdlib>

std::string url_decode(const std::string& str) {
    std::string result;
    for (size_t i = 0; i < str.length(); ++i) {
        if (str[i] == '+') {
            result += ' ';
        } else if (str[i] == '%' && i + 2 < str.length()) {
            char hex[3] = { str[i+1], str[i+2], 0 };
            char decoded_char = static_cast<char>(std::strtol(hex, nullptr, 16));
            result += decoded_char;
            i += 2;
        } else {
            result += str[i];
        }
    }
    return result;
}

std::map<std::string, std::string> parse_query_params(const char* pParams) {
    std::map<std::string, std::string> params;
    if (pParams == nullptr) return params;

    std::string query(pParams);
    std::istringstream ss(query);
    std::string pair;

    while (std::getline(ss, pair, '&')) {
        size_t eq_pos = pair.find('=');
        if (eq_pos == std::string::npos) {
            // key only, no value
            std::string key = url_decode(pair);
            params[key] = "";
        } else {
            std::string key = url_decode(pair.substr(0, eq_pos));
            std::string value = url_decode(pair.substr(eq_pos + 1));
            params[key] = value;
        }
    }
    return params;
}

