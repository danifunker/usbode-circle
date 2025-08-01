#ifndef COMMANDLINE_H
#define COMMANDLINE_H

#define MAX_LINE_LEN 1024
#define MAX_PAIRS 64
#define MAX_KEY_LEN 64
#define MAX_VALUE_LEN 256

struct Pair {
    char key[MAX_KEY_LEN];
    char value[MAX_VALUE_LEN];
};

class CmdLine {
public:
    CmdLine();

    bool Load(const char* filename);
    bool Save(const char* filename) const;

    const char* GetValue(const char* key) const;
    bool SetValue(const char* key, const char* value);

    /*
    int GetLogLevel() const;
    bool SetLogLevel(int level);
    */

private:
    Pair pairs[MAX_PAIRS];
    int count;

    int find_index(const char* key) const;
};

#endif // COMMANDLINE_H

