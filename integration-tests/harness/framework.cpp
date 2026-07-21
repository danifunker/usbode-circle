//
// framework.cpp
//
#include "framework.h"

#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

namespace
{
    struct TestCase
    {
        const char *name;
        void (*fn)();
        const char *group;
    };

    std::vector<TestCase> &Tests()
    {
        static std::vector<TestCase> tests;
        return tests;
    }

    int g_nFailures = 0;
    int g_nFailuresInCurrent = 0;
    const char *g_pCurrentTest = "";

    struct GroupTally
    {
        const char *label;
        int run;
        int failed;
    };

    // Heading each test file's cases are listed under. A file missing from
    // this table falls back to its own basename, so adding a test file gives
    // a usable (if unpolished) group without touching the framework.
    struct GroupName
    {
        const char *file;
        const char *label;
    };

    const GroupName kGroupNames[] = {
        {"test_protocol", "USB Bulk-Only Transport"},
        {"test_basics", "Core SCSI commands"},
        {"test_read10", "SCSI read commands"},
        {"test_readtoc", "TOC and disc description"},
        {"test_modesense", "MODE SENSE"},
        {"test_mediacontrol", "Mount, eject and MODE SELECT"},
        {"test_mediachange", "Media change and disc swap"},
        {"test_audio", "CD audio playback"},
        {"test_cuequirks", "Cue sheet parsing"},
        {"test_badimages", "Damaged and truncated images"},
        {"test_toolbox", "Vendor toolbox commands"},
        {"test_realimages", "Real disc images"},
    };

    // "test-suite/test_read10.cpp" -> "SCSI read commands"
    const char *GroupForFile(const char *pFile)
    {
        std::string base(pFile ? pFile : "");
        size_t slash = base.find_last_of("/\\");
        if (slash != std::string::npos)
        {
            base = base.substr(slash + 1);
        }
        size_t dot = base.find_last_of('.');
        if (dot != std::string::npos)
        {
            base = base.substr(0, dot);
        }

        for (const GroupName &g : kGroupNames)
        {
            if (base == g.file)
            {
                return g.label;
            }
        }

        // Leaked deliberately: the runner holds the pointer for the whole run.
        return strdup(base.c_str());
    }
}

int RegisterTest(const char *pName, void (*pfnTest)(), const char *pFile)
{
    Tests().push_back({pName, pfnTest, GroupForFile(pFile)});
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

    // Registration order is link order, so a file's cases arrive together and
    // a heading per change of group is enough to keep them under one banner.
    const char *pCurrentGroup = nullptr;
    std::vector<GroupTally> tallies;

    for (const auto &test : Tests())
    {
        if (pCurrentGroup == nullptr || strcmp(pCurrentGroup, test.group) != 0)
        {
            pCurrentGroup = test.group;
            printf("\n%s\n", test.group);
            tallies.push_back({test.group, 0, 0});
        }

        g_pCurrentTest = test.name;
        g_nFailuresInCurrent = 0;
        if (getenv("USBODE_TEST_TRACE") != nullptr)
        {
            printf("  run  %s\n", test.name);
            fflush(stdout);
        }
        test.fn();
        nRun++;
        tallies.back().run++;
        if (g_nFailuresInCurrent > 0)
        {
            nFailedTests++;
            tallies.back().failed++;
        }
        else
        {
            printf("  ok   %s\n", test.name);
        }
    }

    printf("\nSummary\n");
    for (const GroupTally &t : tallies)
    {
        printf("  %-32s %2d/%-2d%s\n", t.label, t.run - t.failed, t.run,
               t.failed > 0 ? "  FAILED" : "");
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
