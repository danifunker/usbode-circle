//
// framework.h
//
// Micro test framework: TEST() registers a case, CHECK*() records
// failures with context, main() runs everything and returns nonzero if
// anything failed.
//
#ifndef _test_host_framework_h
#define _test_host_framework_h

#include <stddef.h>
#include <stdint.h>

#include <sstream>
#include <string>

int RegisterTest(const char *pName, void (*pfnTest)());
void ReportFailure(const char *pFile, int nLine, const std::string &message);
int RunAllTests();

void CheckBytesImpl(const char *pFile, int nLine, const char *pWhat,
                    const uint8_t *pActual, size_t nActualLen,
                    const uint8_t *pExpected, size_t nExpectedLen);

#define TEST(name)                                                  \
    static void test_##name();                                      \
    static const int reg_##name = RegisterTest(#name, test_##name); \
    static void test_##name()

#define CHECK(cond)                                                 \
    do                                                              \
    {                                                               \
        if (!(cond))                                                \
        {                                                           \
            ReportFailure(__FILE__, __LINE__, "CHECK failed: " #cond); \
        }                                                           \
    } while (0)

#define CHECK_EQ(actual, expected)                                          \
    do                                                                      \
    {                                                                       \
        auto _a = (actual);                                                 \
        auto _e = (expected);                                               \
        if (!(_a == _e))                                                    \
        {                                                                   \
            std::ostringstream _os;                                         \
            _os << "CHECK_EQ failed: " #actual " == " #expected             \
                << "  (actual " << +_a << " / 0x" << std::hex << +_a        \
                << ", expected " << std::dec << +_e << " / 0x" << std::hex  \
                << +_e << ")";                                              \
            ReportFailure(__FILE__, __LINE__, _os.str());                   \
        }                                                                   \
    } while (0)

#define CHECK_BYTES(actual, actualLen, expected, expectedLen) \
    CheckBytesImpl(__FILE__, __LINE__, #actual, (const uint8_t *)(actual), (actualLen), \
                   (const uint8_t *)(expected), (expectedLen))

#endif
