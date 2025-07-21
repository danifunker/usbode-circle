#include "cmdline.h"
#include <circle/logger.h>
#include <fatfs/ff.h>
#include <circle/util.h>
#include <scsitbservice/scsitbservice.h>

LOGMODULE("cmdline");

CmdLine::CmdLine()
    : count(0)
{
}

bool CmdLine::Load(const char* filename) {
    FIL file;
    FRESULT res;
    UINT bytesRead;
    char line[MAX_LINE_LEN];

    res = f_open(&file, filename, FA_READ);
    if (res != FR_OK) {
        return false;
    }

    res = f_read(&file, line, sizeof(line) - 1, &bytesRead);
    f_close(&file);
    if (res != FR_OK || bytesRead == 0) {
        return false;
    }

    line[bytesRead] = '\0';  // Null-terminate the buffer

    // Chomp trailing newline(s)
    char* p = line + strlen(line) - 1;
    while (p >= line && (*p == '\n' || *p == '\r')) {
        *p-- = '\0';
    }

    count = 0;
    char* saveptr = nullptr;
    char* token = strtok_r(line, " ", &saveptr);
    while (token && count < MAX_PAIRS) {
        char* equal = strstr(token, "=");
        if (equal) {
            size_t key_len = equal - token;
            size_t value_len = strlen(equal + 1);

            if (key_len < MAX_KEY_LEN && value_len < MAX_VALUE_LEN) {
                memcpy(pairs[count].key, token, key_len);
                pairs[count].key[key_len] = '\0';
                strcpy(pairs[count].value, equal + 1);
                ++count;
            }
        }
        token = strtok_r(nullptr, " ", &saveptr);
    }
    return true;
}

bool CmdLine::Save(const char* filename) const {
    FIL file;
    FRESULT res;
    UINT bytesWritten;

    LOGNOTE("Opening file %s", filename);

    res = f_open(&file, filename, FA_WRITE | FA_CREATE_ALWAYS);
    if (res != FR_OK) {
        LOGNOTE("Failed to open file");
        return false;
    }

    LOGNOTE("Opened file");

    char line[MAX_LINE_LEN] = {0};  // Clear buffer to prevent garbage
    size_t pos = 0;

    for (int i = 0; i < count; ++i) {
        int n = sprintf(line + pos, "%s=%s", pairs[i].key, pairs[i].value);
        if (n < 0 || pos + (size_t)n >= sizeof(line)) {
            f_close(&file);
            LOGNOTE("Failed: buffer overflow");
            return false;
        }
        pos += n;

        if (i < count - 1) {
            if (pos + 1 < sizeof(line)) {
                line[pos++] = ' ';
            } else {
                f_close(&file);
                LOGNOTE("Failed: no room for space");
                return false;
            }
        }
    }

    if (pos + 1 < sizeof(line)) {
        line[pos++] = '\n';
    } else {
        f_close(&file);
        LOGNOTE("Failed: no room for newline");
        return false;
    }

    LOGNOTE("Writing %u bytes: '%.*s'", (unsigned)pos, (int)pos, line);

    res = f_write(&file, line, pos, &bytesWritten);
    f_close(&file);

    if (res != FR_OK) {
        LOGNOTE("f_write failed: %d", res);
        return false;
    }

    if (bytesWritten != pos) {
        LOGNOTE("Write incomplete: %u/%u bytes", (unsigned)bytesWritten, (unsigned)pos);
        return false;
    }

    LOGNOTE("Written successfully");
    return true;
}

const char* CmdLine::GetValue(const char* key) const {
    int i = find_index(key);
    return (i >= 0) ? pairs[i].value : nullptr;
}

bool CmdLine::SetValue(const char* key, const char* value) {
    int i = find_index(key);
    if (i >= 0) {
        strncpy(pairs[i].value, value, MAX_VALUE_LEN - 1);
        pairs[i].value[MAX_VALUE_LEN - 1] = '\0';
        return true;
    } else if (count < MAX_PAIRS) {
        strncpy(pairs[count].key, key, MAX_KEY_LEN - 1);
        pairs[count].key[MAX_KEY_LEN - 1] = '\0';
        strncpy(pairs[count].value, value, MAX_VALUE_LEN - 1);
        pairs[count].value[MAX_VALUE_LEN - 1] = '\0';
        ++count;
        return true;
    }
    return false;
}

/*
int CmdLine::GetLogLevel() const {
    const char* val = GetValue("loglevel");
    return val ? atoi(val) : -1;
}

bool CmdLine::SetLogLevel(int level) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", level);
    return SetValue("loglevel", buf);
}
*/

int CmdLine::find_index(const char* key) const {
    for (int i = 0; i < count; ++i) {
        if (strcmp(pairs[i].key, key) == 0) {
            return i;
        }
    }
    return -1;
}

