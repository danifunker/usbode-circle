//
// framework.cpp
//
#include "framework.h"

#include <stdio.h>

#include <vector>

namespace
{
    struct TestCase
    {
        const char *name;
        void (*fn)();
    };

    std::vector<TestCase> &Tests()
    {
        static std::vector<TestCase> tests;
        return tests;
    }

    int g_nFailures = 0;
    int g_nFailuresInCurrent = 0;
    const char *g_pCurrentTest = "";
}

int RegisterTest(const char *pName, void (*pfnTest)())
{
    Tests().push_back({pName, pfnTest});
    return (int)Tests().size();
}

void ReportFailure(const char *pFile, int nLine, const std::string &message)
{
    printf("  FAIL [%s] %s:%d: %s\n", g_pCurrentTest, pFile, nLine, message.c_str());
    g_nFailures++;
    g_nFailuresInCurrent++;
}

static void HexDump(const char *pLabel, const uint8_t *pData, size_t nLen)
{
    printf("    %s (%zu bytes):\n", pLabel, nLen);
    for (size_t i = 0; i < nLen; i += 16)
    {
        printf("      [%04zx]", i);
        for (size_t j = i; j < i + 16 && j < nLen; j++)
        {
            printf(" %02x", pData[j]);
        }
        printf("\n");
    }
}

void CheckBytesImpl(const char *pFile, int nLine, const char *pWhat,
                    const uint8_t *pActual, size_t nActualLen,
                    const uint8_t *pExpected, size_t nExpectedLen)
{
    bool lengthOk = nActualLen == nExpectedLen;
    bool bytesOk = lengthOk;
    size_t firstDiff = 0;

    if (lengthOk)
    {
        for (size_t i = 0; i < nActualLen; i++)
        {
            if (pActual[i] != pExpected[i])
            {
                bytesOk = false;
                firstDiff = i;
                break;
            }
        }
    }

    if (bytesOk)
    {
        return;
    }

    std::ostringstream os;
    os << "CHECK_BYTES failed for " << pWhat;
    if (!lengthOk)
    {
        os << ": length " << nActualLen << ", expected " << nExpectedLen;
    }
    else
    {
        os << ": first difference at offset " << firstDiff;
    }
    ReportFailure(pFile, nLine, os.str());
    HexDump("actual", pActual, nActualLen);
    HexDump("expected", pExpected, nExpectedLen);
}

int RunAllTests()
{
    int nRun = 0;
    int nFailedTests = 0;

    for (const auto &test : Tests())
    {
        g_pCurrentTest = test.name;
        g_nFailuresInCurrent = 0;
        if (getenv("USBODE_TEST_TRACE") != nullptr)
        {
            printf("  run  %s\n", test.name);
            fflush(stdout);
        }
        test.fn();
        nRun++;
        if (g_nFailuresInCurrent > 0)
        {
            nFailedTests++;
        }
        else
        {
            printf("  ok   %s\n", test.name);
        }
    }

    printf("\n%d tests, %d failed (%d individual check failures)\n",
           nRun, nFailedTests, g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}

int main()
{
    setvbuf(stdout, nullptr, _IONBF, 0); // keep output on crashes
    printf("USBODE host regression tests\n");
    return RunAllTests();
}
