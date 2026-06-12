//
// Host-side tests for the subchannel helpers.
//
// Build & run:  cc -o subchannel_test subchannel_test.cpp ../addon/discimage/subchannel.cpp -lstdc++ && ./subchannel_test
//
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../addon/discimage/subchannel.h"

static int failures = 0;

#define CHECK_EQ(actual, expected, what)                                            \
    do {                                                                            \
        unsigned long long a = (unsigned long long)(actual);                        \
        unsigned long long e = (unsigned long long)(expected);                      \
        if (a != e) {                                                               \
            printf("FAIL %s:%d %s: got 0x%llx, expected 0x%llx\n", __func__,       \
                   __LINE__, what, a, e);                                           \
            failures++;                                                             \
        }                                                                           \
    } while (0)

static void test_crc16() {
    // CRC-16/XMODEM("123456789") = 0x31C3 is the standard check value for
    // poly 0x1021 / init 0; the Q channel stores it complemented.
    const uint8_t msg[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    CHECK_EQ(SubQCRC16(msg, 9), (uint16_t)~0x31C3, "complemented XMODEM check value");
}

static void test_interleave_roundtrip() {
    uint8_t linear[96], raw[96], back[96];
    for (int i = 0; i < 96; i++)
        linear[i] = (uint8_t)(i * 37 + 11);

    InterleavePW96(linear, raw);
    DeinterleavePW96(raw, back);
    CHECK_EQ(memcmp(linear, back, 96), 0, "roundtrip");

    // A pure-P linear block must set bit 7 of every raw symbol and no others
    memset(linear, 0, 96);
    memset(&linear[0], 0xFF, 12);
    InterleavePW96(linear, raw);
    for (int i = 0; i < 96; i++) {
        if (raw[i] != 0x80) {
            printf("FAIL pure-P symbol %d = 0x%02x\n", i, raw[i]);
            failures++;
            break;
        }
    }
}

static void test_build_q12() {
    QSynthInfo info = {};
    info.control = 0x04; // data track
    info.tno = 21;
    info.index = 1;
    info.rel_lba = 0;
    info.abs_lba = 45867; // +150 -> 46017 = 10:13:42

    uint8_t q[12];
    BuildQ12(info, q);

    CHECK_EQ(q[0], 0x41, "control/adr");
    CHECK_EQ(q[1], 0x21, "tno BCD");
    CHECK_EQ(q[2], 0x01, "index");
    CHECK_EQ(q[3], 0x00, "rel min");
    CHECK_EQ(q[4], 0x00, "rel sec");
    CHECK_EQ(q[5], 0x00, "rel frame");
    CHECK_EQ(q[6], 0x00, "zero");
    CHECK_EQ(q[7], 0x10, "abs min BCD");
    CHECK_EQ(q[8], 0x13, "abs sec BCD");
    CHECK_EQ(q[9], 0x42, "abs frame BCD");

    // CRC must verify: recompute over the first 10 bytes
    uint16_t crc = SubQCRC16(q, 10);
    CHECK_EQ(q[10], crc >> 8, "crc hi");
    CHECK_EQ(q[11], crc & 0xFF, "crc lo");

    // Raw block: Q extractable again, P solid in pregap
    info.index = 0;
    info.rel_lba = 150;
    uint8_t raw[96], linear[96];
    BuildRawPW96(info, raw);
    DeinterleavePW96(raw, linear);
    CHECK_EQ(linear[0], 0xFF, "P solid in pregap");
    CHECK_EQ(linear[12], 0x41, "Q first byte");
    CHECK_EQ(linear[14], 0x00, "index 0");
    CHECK_EQ(linear[16], 0x02, "rel sec = 2 (150 frames)");
    CHECK_EQ(linear[17], 0x00, "rel frame = 0");
}

int main() {
    test_crc16();
    test_interleave_roundtrip();
    test_build_q12();

    if (failures) {
        printf("%d FAILURE(S)\n", failures);
        return 1;
    }
    printf("all subchannel tests passed\n");
    return 0;
}
