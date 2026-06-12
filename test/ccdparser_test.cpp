//
// Host-side tests for the CloneCD .ccd parser.
//
// Build & run:  cc -o ccdparser_test ccdparser_test.cpp ../addon/ccdparser/ccdparser.cpp -lstdc++ && ./ccdparser_test
//
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../addon/ccdparser/ccdparser.h"

static int failures = 0;

#define CHECK_EQ(actual, expected, what)                                            \
    do {                                                                            \
        long long a = (long long)(actual);                                          \
        long long e = (long long)(expected);                                        \
        if (a != e) {                                                               \
            printf("FAIL %s:%d %s: got %lld, expected %lld\n", __func__, __LINE__, \
                   what, a, e);                                                     \
            failures++;                                                             \
        }                                                                           \
    } while (0)

// Shaped like real CloneCD output: hex Point values, CRLF tolerated,
// data track + audio track, [TRACK] sections with INDEX entries.
static const char *sample_ccd =
    "[CloneCD]\r\n"
    "Version=3\r\n"
    "[Disc]\r\n"
    "TocEntries=6\r\n"
    "Sessions=1\r\n"
    "DataTracksScrambled=0\r\n"
    "CDTextLength=0\r\n"
    "CATALOG=1234567890123\r\n"
    "[Session 1]\r\n"
    "PreGapMode=1\r\n"
    "PreGapSubC=0\r\n"
    "[Entry 0]\r\n"
    "Session=1\r\n"
    "Point=0xa0\r\n"
    "ADR=0x01\r\n"
    "Control=0x04\r\n"
    "TrackNo=0\r\n"
    "PMin=1\r\n"
    "PSec=0\r\n"
    "PFrame=0\r\n"
    "PLBA=4350\r\n"
    "[Entry 1]\r\n"
    "Session=1\r\n"
    "Point=0xa1\r\n"
    "Control=0x04\r\n"
    "PMin=2\r\n"
    "PLBA=8850\r\n"
    "[Entry 2]\r\n"
    "Session=1\r\n"
    "Point=0xa2\r\n"
    "Control=0x04\r\n"
    "PMin=56\r\n"
    "PSec=32\r\n"
    "PFrame=74\r\n"
    "PLBA=254324\r\n"
    "[Entry 3]\r\n"
    "Session=1\r\n"
    "Point=0x01\r\n"
    "ADR=0x01\r\n"
    "Control=0x04\r\n"
    "ALBA=-150\r\n"
    "PLBA=0\r\n"
    "[Entry 4]\r\n"
    "Session=1\r\n"
    "Point=0x02\r\n"
    "ADR=0x01\r\n"
    "Control=0x00\r\n"
    "ALBA=183764\r\n"
    "PLBA=183914\r\n"
    "[TRACK 1]\r\n"
    "MODE=1\r\n"
    "INDEX 1=0\r\n"
    "[TRACK 2]\r\n"
    "MODE=0\r\n"
    "ISRC=USRC17607839\r\n"
    "INDEX 0=183914\r\n"
    "INDEX 1=184064\r\n";

static void test_parse() {
    CCDParser p(sample_ccd);

    CHECK_EQ(p.isValid(), 1, "valid");
    CHECK_EQ(p.getNumSessions(), 1, "sessions");
    CHECK_EQ(p.isDataScrambled(), 0, "not scrambled");
    CHECK_EQ(strcmp(p.getCatalog(), "1234567890123"), 0, "catalog");
    CHECK_EQ(p.getNumEntries(), 5, "entries");

    const CCD_Entry *a2 = p.getEntry(2);
    CHECK_EQ(a2->point, 0xA2, "a2 point (hex parse)");
    CHECK_EQ(a2->plba, 254324, "a2 leadout plba");

    const CCD_Entry *t1 = p.getEntry(3);
    CHECK_EQ(t1->point, 1, "t1 point");
    CHECK_EQ(t1->control, 4, "t1 control = data");
    CHECK_EQ(t1->alba, -150, "t1 alba negative");
    CHECK_EQ(t1->plba, 0, "t1 plba");

    const CCD_Entry *t2 = p.getEntry(4);
    CHECK_EQ(t2->point, 2, "t2 point");
    CHECK_EQ(t2->control, 0, "t2 control = audio");
    CHECK_EQ(t2->plba, 183914, "t2 plba");

    const CCD_Track *td1 = p.getTrack(1);
    CHECK_EQ(td1 != nullptr, 1, "track 1 details present");
    CHECK_EQ(td1->mode, 1, "track 1 mode");
    CHECK_EQ(td1->index0, -1, "track 1 no index0");
    CHECK_EQ(td1->index1, 0, "track 1 index1");

    const CCD_Track *td2 = p.getTrack(2);
    CHECK_EQ(td2->mode, 0, "track 2 mode");
    CHECK_EQ(td2->index0, 183914, "track 2 index0");
    CHECK_EQ(td2->index1, 184064, "track 2 index1");
    CHECK_EQ(strcmp(td2->isrc, "USRC17607839"), 0, "track 2 isrc");

    CHECK_EQ((long long)p.getTrack(3), 0, "no track 3");
}

static void test_invalid() {
    CCDParser empty("");
    CHECK_EQ(empty.isValid(), 0, "empty invalid");

    CCDParser noTracks("[CloneCD]\nVersion=3\n[Disc]\nTocEntries=0\n");
    CHECK_EQ(noTracks.isValid(), 0, "no track entries invalid");
}

int main() {
    test_parse();
    test_invalid();

    if (failures) {
        printf("%d FAILURE(S)\n", failures);
        return 1;
    }
    printf("all ccdparser tests passed\n");
    return 0;
}
