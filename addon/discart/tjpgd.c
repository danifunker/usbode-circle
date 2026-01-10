/*----------------------------------------------------------------------------/
/ TJpgDec - Tiny JPEG Decompressor R0.03                      (C)ChaN, 2021
/-----------------------------------------------------------------------------/
/ Standalone version for USBODE disc art display (no LVGL dependency)
/
/ The TJpgDec is a generic JPEG decompressor module for tiny embedded systems.
/ This is a free software that opened for education, research and commercial
/  developments under license policy of following terms.
/
/  Copyright (C) 2021, ChaN, all right reserved.
/
/ * The TJpgDec module is a free software and there is NO WARRANTY.
/ * No restriction on use. You can use, modify and redistribute it for
/   personal, non-profit or commercial products UNDER YOUR RESPONSIBILITY.
/ * Redistributions of source code must retain the above copyright notice.
/----------------------------------------------------------------------------*/

#include "tjpgd.h"

#if JD_FASTDECODE == 2
    #define HUFF_BIT    10
    #define HUFF_LEN    (1 << HUFF_BIT)
    #define HUFF_MASK   (HUFF_LEN - 1)
#endif

static const uint8_t Zig[64] = {
    0,  1,  8, 16,  9,  2,  3, 10, 17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63
};

static const uint16_t Ipsf[64] = {
    (uint16_t)(1.00000 * 8192), (uint16_t)(1.38704 * 8192), (uint16_t)(1.30656 * 8192), (uint16_t)(1.17588 * 8192), (uint16_t)(1.00000 * 8192), (uint16_t)(0.78570 * 8192), (uint16_t)(0.54120 * 8192), (uint16_t)(0.27590 * 8192),
    (uint16_t)(1.38704 * 8192), (uint16_t)(1.92388 * 8192), (uint16_t)(1.81226 * 8192), (uint16_t)(1.63099 * 8192), (uint16_t)(1.38704 * 8192), (uint16_t)(1.08979 * 8192), (uint16_t)(0.75066 * 8192), (uint16_t)(0.38268 * 8192),
    (uint16_t)(1.30656 * 8192), (uint16_t)(1.81226 * 8192), (uint16_t)(1.70711 * 8192), (uint16_t)(1.53636 * 8192), (uint16_t)(1.30656 * 8192), (uint16_t)(1.02656 * 8192), (uint16_t)(0.70711 * 8192), (uint16_t)(0.36048 * 8192),
    (uint16_t)(1.17588 * 8192), (uint16_t)(1.63099 * 8192), (uint16_t)(1.53636 * 8192), (uint16_t)(1.38268 * 8192), (uint16_t)(1.17588 * 8192), (uint16_t)(0.92388 * 8192), (uint16_t)(0.63638 * 8192), (uint16_t)(0.32442 * 8192),
    (uint16_t)(1.00000 * 8192), (uint16_t)(1.38704 * 8192), (uint16_t)(1.30656 * 8192), (uint16_t)(1.17588 * 8192), (uint16_t)(1.00000 * 8192), (uint16_t)(0.78570 * 8192), (uint16_t)(0.54120 * 8192), (uint16_t)(0.27590 * 8192),
    (uint16_t)(0.78570 * 8192), (uint16_t)(1.08979 * 8192), (uint16_t)(1.02656 * 8192), (uint16_t)(0.92388 * 8192), (uint16_t)(0.78570 * 8192), (uint16_t)(0.61732 * 8192), (uint16_t)(0.42522 * 8192), (uint16_t)(0.21677 * 8192),
    (uint16_t)(0.54120 * 8192), (uint16_t)(0.75066 * 8192), (uint16_t)(0.70711 * 8192), (uint16_t)(0.63638 * 8192), (uint16_t)(0.54120 * 8192), (uint16_t)(0.42522 * 8192), (uint16_t)(0.29290 * 8192), (uint16_t)(0.14932 * 8192),
    (uint16_t)(0.27590 * 8192), (uint16_t)(0.38268 * 8192), (uint16_t)(0.36048 * 8192), (uint16_t)(0.32442 * 8192), (uint16_t)(0.27590 * 8192), (uint16_t)(0.21678 * 8192), (uint16_t)(0.14932 * 8192), (uint16_t)(0.07612 * 8192)
};

#if JD_TBLCLIP
#define BYTECLIP(v) Clip8[(unsigned int)(v) & 0x3FF]
static const uint8_t Clip8[1024] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
    32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
    64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95,
    96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127,
    128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159,
    160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175, 176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191,
    192, 193, 194, 195, 196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207, 208, 209, 210, 211, 212, 213, 214, 215, 216, 217, 218, 219, 220, 221, 222, 223,
    224, 225, 226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 239, 240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};
#else
static uint8_t BYTECLIP(int val) {
    if(val < 0) return 0;
    if(val > 255) return 255;
    return (uint8_t)val;
}
#endif

static void * alloc_pool(JDEC * jd, size_t ndata) {
    char * rp = 0;
    ndata = (ndata + 3) & ~3;
    if(jd->sz_pool >= ndata) {
        jd->sz_pool -= ndata;
        rp = (char *)jd->pool;
        jd->pool = (void *)(rp + ndata);
    }
    return (void *)rp;
}

static JRESULT create_qt_tbl(JDEC * jd, const uint8_t * data, size_t ndata) {
    unsigned int i, zi;
    uint8_t d;
    int32_t * pb;

    while(ndata) {
        if(ndata < 65) return JDR_FMT1;
        ndata -= 65;
        d = *data++;
        if(d & 0xF0) return JDR_FMT1;
        i = d & 3;
        pb = alloc_pool(jd, 64 * sizeof(int32_t));
        if(!pb) return JDR_MEM1;
        jd->qttbl[i] = pb;
        for(i = 0; i < 64; i++) {
            zi = Zig[i];
            pb[zi] = (int32_t)((uint32_t) * data++ * Ipsf[zi]);
        }
    }
    return JDR_OK;
}

static JRESULT create_huffman_tbl(JDEC * jd, const uint8_t * data, size_t ndata) {
    unsigned int i, j, b, cls, num;
    size_t np;
    uint8_t d, * pb, * pd;
    uint16_t hc, * ph;

    while(ndata) {
        if(ndata < 17) return JDR_FMT1;
        ndata -= 17;
        d = *data++;
        if(d & 0xEE) return JDR_FMT1;
        cls = d >> 4;
        num = d & 0x0F;
        pb = alloc_pool(jd, 16);
        if(!pb) return JDR_MEM1;
        jd->huffbits[num][cls] = pb;
        for(np = i = 0; i < 16; i++) {
            np += (pb[i] = *data++);
        }
        ph = alloc_pool(jd, np * sizeof(uint16_t));
        if(!ph) return JDR_MEM1;
        jd->huffcode[num][cls] = ph;
        hc = 0;
        for(j = i = 0; i < 16; i++) {
            b = pb[i];
            while(b--) ph[j++] = hc++;
            hc <<= 1;
        }
        if(ndata < np) return JDR_FMT1;
        ndata -= np;
        pd = alloc_pool(jd, np);
        if(!pd) return JDR_MEM1;
        jd->huffdata[num][cls] = pd;
        for(i = 0; i < np; i++) {
            d = *data++;
            if(!cls && d > 11) return JDR_FMT1;
            pd[i] = d;
        }
#if JD_FASTDECODE == 2
        {
            unsigned int span, td, ti;
            uint16_t * tbl_ac = 0;
            uint8_t * tbl_dc = 0;
            if(cls) {
                tbl_ac = alloc_pool(jd, HUFF_LEN * sizeof(uint16_t));
                if(!tbl_ac) return JDR_MEM1;
                jd->hufflut_ac[num] = tbl_ac;
                memset(tbl_ac, 0xFF, HUFF_LEN * sizeof(uint16_t));
            } else {
                tbl_dc = alloc_pool(jd, HUFF_LEN * sizeof(uint8_t));
                if(!tbl_dc) return JDR_MEM1;
                jd->hufflut_dc[num] = tbl_dc;
                memset(tbl_dc, 0xFF, HUFF_LEN * sizeof(uint8_t));
            }
            for(i = b = 0; b < HUFF_BIT; b++) {
                for(j = pb[b]; j; j--) {
                    ti = ph[i] << (HUFF_BIT - 1 - b) & HUFF_MASK;
                    if(cls) {
                        td = pd[i++] | ((b + 1) << 8);
                        for(span = 1 << (HUFF_BIT - 1 - b); span; span--, tbl_ac[ti++] = (uint16_t)td) ;
                    } else {
                        td = pd[i++] | ((b + 1) << 4);
                        for(span = 1 << (HUFF_BIT - 1 - b); span; span--, tbl_dc[ti++] = (uint8_t)td) ;
                    }
                }
            }
            jd->longofs[num][cls] = i;
        }
#endif
    }
    return JDR_OK;
}

static int huffext(JDEC * jd, unsigned int id, unsigned int cls) {
    size_t dc = jd->dctr;
    uint8_t * dp = jd->dptr;
    unsigned int d, flg = 0;

#if JD_FASTDECODE == 0
    uint8_t bm, nd, bl;
    const uint8_t * hb = jd->huffbits[id][cls];
    const uint16_t * hc = jd->huffcode[id][cls];
    const uint8_t * hd = jd->huffdata[id][cls];

    bm = jd->dbit;
    d = 0;
    bl = 16;
    do {
        if(!bm) {
            if(!dc) {
                dp = jd->inbuf;
                dc = jd->infunc(jd, dp, JD_SZBUF);
                if(!dc) return 0 - (int)JDR_INP;
            } else {
                dp++;
            }
            dc--;
            if(flg) {
                flg = 0;
                if(*dp != 0) return 0 - (int)JDR_FMT1;
                *dp = 0xFF;
            } else {
                if(*dp == 0xFF) {
                    flg = 1;
                    continue;
                }
            }
            bm = 0x80;
        }
        d <<= 1;
        if(*dp & bm) d++;
        bm >>= 1;
        for(nd = *hb++; nd; nd--) {
            if(d == *hc++) {
                jd->dbit = bm;
                jd->dctr = dc;
                jd->dptr = dp;
                return *hd;
            }
            hd++;
        }
        bl--;
    } while(bl);
#else
    const uint8_t * hb, * hd;
    const uint16_t * hc;
    unsigned int nc, bl, wbit = jd->dbit % 32;
    uint32_t w = jd->wreg & ((1UL << wbit) - 1);

    while(wbit < 16) {
        if(jd->marker) {
            d = 0xFF;
        } else {
            if(!dc) {
                dp = jd->inbuf;
                dc = jd->infunc(jd, dp, JD_SZBUF);
                if(!dc) return 0 - (int)JDR_INP;
            }
            d = *dp++;
            dc--;
            if(flg) {
                flg = 0;
                if(d != 0) jd->marker = d;
                d = 0xFF;
            } else {
                if(d == 0xFF) {
                    flg = 1;
                    continue;
                }
            }
        }
        w = w << 8 | d;
        wbit += 8;
    }
    jd->dctr = dc;
    jd->dptr = dp;
    jd->wreg = w;

#if JD_FASTDECODE == 2
    d = (unsigned int)(w >> (wbit - HUFF_BIT));
    if(cls) {
        d = jd->hufflut_ac[id][d];
        if(d != 0xFFFF) {
            jd->dbit = wbit - (d >> 8);
            return d & 0xFF;
        }
    } else {
        d = jd->hufflut_dc[id][d];
        if(d != 0xFF) {
            jd->dbit = wbit - (d >> 4);
            return d & 0xF;
        }
    }
    hb = jd->huffbits[id][cls] + HUFF_BIT;
    hc = jd->huffcode[id][cls] + jd->longofs[id][cls];
    hd = jd->huffdata[id][cls] + jd->longofs[id][cls];
    bl = HUFF_BIT + 1;
#else
    hb = jd->huffbits[id][cls];
    hc = jd->huffcode[id][cls];
    hd = jd->huffdata[id][cls];
    bl = 1;
#endif
    for(; bl <= 16; bl++) {
        nc = *hb++;
        if(nc) {
            d = w >> (wbit - bl);
            do {
                if(d == *hc++) {
                    jd->dbit = wbit - bl;
                    return *hd;
                }
                hd++;
            } while(--nc);
        }
    }
#endif
    return 0 - (int)JDR_FMT1;
}

static int bitext(JDEC * jd, unsigned int nbit) {
    size_t dc = jd->dctr;
    uint8_t * dp = jd->dptr;
    unsigned int d, flg = 0;

#if JD_FASTDECODE == 0
    uint8_t mbit = jd->dbit;
    d = 0;
    do {
        if(!mbit) {
            if(!dc) {
                dp = jd->inbuf;
                dc = jd->infunc(jd, dp, JD_SZBUF);
                if(!dc) return 0 - (int)JDR_INP;
            } else {
                dp++;
            }
            dc--;
            if(flg) {
                flg = 0;
                if(*dp != 0) return 0 - (int)JDR_FMT1;
                *dp = 0xFF;
            } else {
                if(*dp == 0xFF) {
                    flg = 1;
                    continue;
                }
            }
            mbit = 0x80;
        }
        d <<= 1;
        if(*dp & mbit) d |= 1;
        mbit >>= 1;
        nbit--;
    } while(nbit);
    jd->dbit = mbit;
    jd->dctr = dc;
    jd->dptr = dp;
    return (int)d;
#else
    unsigned int wbit = jd->dbit % 32;
    uint32_t w = jd->wreg & ((1UL << wbit) - 1);

    while(wbit < nbit) {
        if(jd->marker) {
            d = 0xFF;
        } else {
            if(!dc) {
                dp = jd->inbuf;
                dc = jd->infunc(jd, dp, JD_SZBUF);
                if(!dc) return 0 - (int)JDR_INP;
            }
            d = *dp++;
            dc--;
            if(flg) {
                flg = 0;
                if(d != 0) jd->marker = d;
                d = 0xFF;
            } else {
                if(d == 0xFF) {
                    flg = 1;
                    continue;
                }
            }
        }
        w = w << 8 | d;
        wbit += 8;
    }
    jd->wreg = w;
    jd->dbit = wbit - nbit;
    jd->dctr = dc;
    jd->dptr = dp;
    return (int)(w >> ((wbit - nbit) % 32));
#endif
}

JRESULT jd_restart(JDEC * jd, uint16_t rstn) {
    unsigned int i;
    uint8_t * dp = jd->dptr;
    size_t dc = jd->dctr;

#if JD_FASTDECODE == 0
    uint16_t d = 0;
    for(i = 0; i < 2; i++) {
        if(!dc) {
            dp = jd->inbuf;
            dc = jd->infunc(jd, dp, JD_SZBUF);
            if(!dc) return JDR_INP;
        } else {
            dp++;
        }
        dc--;
        d = d << 8 | *dp;
    }
    jd->dptr = dp;
    jd->dctr = dc;
    jd->dbit = 0;
    if((d & 0xFFD8) != 0xFFD0 || (d & 7) != (rstn & 7)) {
        return JDR_FMT1;
    }
#else
    uint16_t marker;
    if(jd->marker) {
        marker = 0xFF00 | jd->marker;
        jd->marker = 0;
    } else {
        marker = 0;
        for(i = 0; i < 2; i++) {
            if(!dc) {
                dp = jd->inbuf;
                dc = jd->infunc(jd, dp, JD_SZBUF);
                if(!dc) return JDR_INP;
            }
            marker = (marker << 8) | *dp++;
            dc--;
        }
        jd->dptr = dp;
        jd->dctr = dc;
    }
    if((marker & 0xFFD8) != 0xFFD0 || (marker & 7) != (rstn & 7)) {
        return JDR_FMT1;
    }
    jd->dbit = 0;
#endif
    jd->dcv[2] = jd->dcv[1] = jd->dcv[0] = 0;
    return JDR_OK;
}

static void block_idct(int32_t * src, jd_yuv_t * dst) {
    const int32_t M13 = (int32_t)(1.41421 * 4096), M2 = (int32_t)(1.08239 * 4096), M4 = (int32_t)(2.61313 * 4096), M5 = (int32_t)(1.84776 * 4096);
    int32_t v0, v1, v2, v3, v4, v5, v6, v7;
    int32_t t10, t11, t12, t13;
    int i;

    for(i = 0; i < 8; i++) {
        v0 = src[8 * 0]; v1 = src[8 * 2]; v2 = src[8 * 4]; v3 = src[8 * 6];
        t10 = v0 + v2; t12 = v0 - v2; t11 = (v1 - v3) * M13 >> 12; v3 += v1; t11 -= v3;
        v0 = t10 + v3; v3 = t10 - v3; v1 = t11 + t12; v2 = t12 - t11;
        v4 = src[8 * 7]; v5 = src[8 * 1]; v6 = src[8 * 5]; v7 = src[8 * 3];
        t10 = v5 - v4; t11 = v5 + v4; t12 = v6 - v7; v7 += v6; v5 = (t11 - v7) * M13 >> 12; v7 += t11;
        t13 = (t10 + t12) * M5 >> 12; v4 = t13 - (t10 * M2 >> 12); v6 = t13 - (t12 * M4 >> 12) - v7; v5 -= v6; v4 -= v5;
        src[8 * 0] = v0 + v7; src[8 * 7] = v0 - v7; src[8 * 1] = v1 + v6; src[8 * 6] = v1 - v6;
        src[8 * 2] = v2 + v5; src[8 * 5] = v2 - v5; src[8 * 3] = v3 + v4; src[8 * 4] = v3 - v4;
        src++;
    }
    src -= 8;
    for(i = 0; i < 8; i++) {
        v0 = src[0] + (128L << 8); v1 = src[2]; v2 = src[4]; v3 = src[6];
        t10 = v0 + v2; t12 = v0 - v2; t11 = (v1 - v3) * M13 >> 12; v3 += v1; t11 -= v3;
        v0 = t10 + v3; v3 = t10 - v3; v1 = t11 + t12; v2 = t12 - t11;
        v4 = src[7]; v5 = src[1]; v6 = src[5]; v7 = src[3];
        t10 = v5 - v4; t11 = v5 + v4; t12 = v6 - v7; v7 += v6; v5 = (t11 - v7) * M13 >> 12; v7 += t11;
        t13 = (t10 + t12) * M5 >> 12; v4 = t13 - (t10 * M2 >> 12); v6 = t13 - (t12 * M4 >> 12) - v7; v5 -= v6; v4 -= v5;
#if JD_FASTDECODE >= 1
        dst[0] = (int16_t)((v0 + v7) >> 8); dst[7] = (int16_t)((v0 - v7) >> 8);
        dst[1] = (int16_t)((v1 + v6) >> 8); dst[6] = (int16_t)((v1 - v6) >> 8);
        dst[2] = (int16_t)((v2 + v5) >> 8); dst[5] = (int16_t)((v2 - v5) >> 8);
        dst[3] = (int16_t)((v3 + v4) >> 8); dst[4] = (int16_t)((v3 - v4) >> 8);
#else
        dst[0] = BYTECLIP((v0 + v7) >> 8); dst[7] = BYTECLIP((v0 - v7) >> 8);
        dst[1] = BYTECLIP((v1 + v6) >> 8); dst[6] = BYTECLIP((v1 - v6) >> 8);
        dst[2] = BYTECLIP((v2 + v5) >> 8); dst[5] = BYTECLIP((v2 - v5) >> 8);
        dst[3] = BYTECLIP((v3 + v4) >> 8); dst[4] = BYTECLIP((v3 - v4) >> 8);
#endif
        dst += 8;
        src += 8;
    }
}

JRESULT jd_mcu_load(JDEC * jd) {
    int32_t * tmp = (int32_t *)jd->workbuf;
    int d, e;
    unsigned int blk, nby, i, bc, z, id, cmp;
    jd_yuv_t * bp;
    const int32_t * dqf;

    nby = jd->msx * jd->msy;
    bp = jd->mcubuf;

    for(blk = 0; blk < nby + 2; blk++) {
        cmp = (blk < nby) ? 0 : blk - nby + 1;
        if(cmp && jd->ncomp != 3) {
            for(i = 0; i < 64; bp[i++] = 128) ;
        } else {
            id = cmp ? 1 : 0;
            d = huffext(jd, id, 0);
            if(d < 0) return (JRESULT)(0 - d);
            bc = (unsigned int)d;
            d = jd->dcv[cmp];
            if(bc) {
                e = bitext(jd, bc);
                if(e < 0) return (JRESULT)(0 - e);
                bc = 1 << (bc - 1);
                if(!(e & bc)) e -= (bc << 1) - 1;
                d += e;
                jd->dcv[cmp] = (int16_t)d;
            }
            dqf = jd->qttbl[jd->qtid[cmp]];
            tmp[0] = d * dqf[0] >> 8;
            memset(&tmp[1], 0, 63 * sizeof(int32_t));
            z = 1;
            do {
                d = huffext(jd, id, 1);
                if(d == 0) break;
                if(d < 0) return (JRESULT)(0 - d);
                bc = (unsigned int)d;
                z += bc >> 4;
                if(z >= 64) return JDR_FMT1;
                if(bc &= 0x0F) {
                    d = bitext(jd, bc);
                    if(d < 0) return (JRESULT)(0 - d);
                    bc = 1 << (bc - 1);
                    if(!(d & bc)) d -= (bc << 1) - 1;
                    i = Zig[z];
                    tmp[i] = d * dqf[i] >> 8;
                }
            } while(++z < 64);

            if(JD_FORMAT != 2 || !cmp) {
                if(z == 1 || (JD_USE_SCALE && jd->scale == 3)) {
                    d = (jd_yuv_t)((*tmp / 256) + 128);
                    if(JD_FASTDECODE >= 1) {
                        for(i = 0; i < 64; bp[i++] = d) ;
                    } else {
                        memset(bp, d, 64);
                    }
                } else {
                    block_idct(tmp, bp);
                }
            }
        }
        bp += 64;
    }
    return JDR_OK;
}

JRESULT jd_mcu_output(JDEC * jd, int (*outfunc)(JDEC *, void *, JRECT *), unsigned int x, unsigned int y) {
    const int CVACC = (sizeof(int) > 2) ? 1024 : 128;
    unsigned int ix, iy, mx, my, rx, ry;
    int yy, cb, cr;
    jd_yuv_t * py, * pc;
    uint8_t * pix;
    JRECT rect;

    mx = jd->msx * 8; my = jd->msy * 8;
    rx = (x + mx <= jd->width) ? mx : jd->width - x;
    ry = (y + my <= jd->height) ? my : jd->height - y;
    if(JD_USE_SCALE) {
        rx >>= jd->scale; ry >>= jd->scale;
        if(!rx || !ry) return JDR_OK;
        x >>= jd->scale; y >>= jd->scale;
    }
    rect.left = x; rect.right = x + rx - 1;
    rect.top = y; rect.bottom = y + ry - 1;

    if(!JD_USE_SCALE || jd->scale != 3) {
        pix = (uint8_t *)jd->workbuf;
        if(JD_FORMAT != 2) {
            for(iy = 0; iy < my; iy++) {
                pc = py = jd->mcubuf;
                if(my == 16) {
                    pc += 64 * 4 + (iy >> 1) * 8;
                    if(iy >= 8) py += 64;
                } else {
                    pc += mx * 8 + iy * 8;
                }
                py += iy * 8;
                for(ix = 0; ix < mx; ix++) {
                    cb = pc[0] - 128; cr = pc[64] - 128;
                    if(mx == 16) {
                        if(ix == 8) py += 64 - 8;
                        pc += ix & 1;
                    } else {
                        pc++;
                    }
                    yy = *py++;
                    *pix++ = BYTECLIP(yy + ((int)(1.772 * CVACC) * cb) / CVACC);
                    *pix++ = BYTECLIP(yy - ((int)(0.344 * CVACC) * cb + (int)(0.714 * CVACC) * cr) / CVACC);
                    *pix++ = BYTECLIP(yy + ((int)(1.402 * CVACC) * cr) / CVACC);
                }
            }
        }
    }

    mx >>= jd->scale;
    if(rx < mx) {
        uint8_t * s, * d;
        unsigned int xi, yi;
        s = d = (uint8_t *)jd->workbuf;
        for(yi = 0; yi < ry; yi++) {
            for(xi = 0; xi < rx; xi++) {
                *d++ = *s++;
                if(JD_FORMAT != 2) { *d++ = *s++; *d++ = *s++; }
            }
            s += (mx - rx) * (JD_FORMAT != 2 ? 3 : 1);
        }
    }

    if(JD_FORMAT == 1) {
        uint8_t * s = (uint8_t *)jd->workbuf;
        uint16_t w, * d = (uint16_t *)s;
        unsigned int n = rx * ry;
        do {
            // YCC->RGB outputs BGR order: B, G, R
            w = *s++ >> 3;           // B at bits [4:0]
            w |= (*s++ & 0xFC) << 3; // G at bits [10:5]
            w |= (*s++ & 0xF8) << 8; // R at bits [15:11]
            *d++ = (w >> 8) | (w << 8);  // Byte-swap for big-endian display
        } while(--n);
    }

    if(outfunc) return outfunc(jd, jd->workbuf, &rect) ? JDR_OK : JDR_INTR;
    return JDR_OK;
}

#define LDB_WORD(ptr) (uint16_t)(((uint16_t)*((uint8_t*)(ptr))<<8)|(uint16_t)*(uint8_t*)((ptr)+1))

JRESULT jd_prepare(JDEC * jd, size_t (*infunc)(JDEC *, uint8_t *, size_t), void * pool, size_t sz_pool, void * dev) {
    uint8_t * seg, b;
    uint16_t marker;
    unsigned int n, i, ofs;
    size_t len;
    JRESULT rc;

    memset(jd, 0, sizeof(JDEC));
    jd->pool = pool;
    jd->pool_original = pool;
    jd->sz_pool = sz_pool;
    jd->infunc = infunc;
    jd->device = dev;

    jd->inbuf = seg = alloc_pool(jd, JD_SZBUF);
    if(!seg) return JDR_MEM1;

    ofs = marker = 0;
    do {
        if(jd->infunc(jd, seg, 1) != 1) return JDR_INP;
        ofs++;
        marker = marker << 8 | seg[0];
    } while(marker != 0xFFD8);

    for(;;) {
        if(jd->infunc(jd, seg, 4) != 4) return JDR_INP;
        marker = LDB_WORD(seg);
        len = LDB_WORD(seg + 2);
        if(len <= 2 || (marker >> 8) != 0xFF) return JDR_FMT1;
        len -= 2;
        ofs += 4 + len;

        switch(marker & 0xFF) {
            case 0xC0:
                if(len > JD_SZBUF) return JDR_MEM2;
                if(jd->infunc(jd, seg, len) != len) return JDR_INP;
                jd->width = LDB_WORD(&seg[3]);
                jd->height = LDB_WORD(&seg[1]);
                jd->ncomp = seg[5];
                if(jd->ncomp != 3 && jd->ncomp != 1) return JDR_FMT3;
                for(i = 0; i < jd->ncomp; i++) {
                    b = seg[7 + 3 * i];
                    if(i == 0) {
                        if(b != 0x11 && b != 0x22 && b != 0x21) return JDR_FMT3;
                        jd->msx = b >> 4; jd->msy = b & 15;
                    } else {
                        if(b != 0x11) return JDR_FMT3;
                    }
                    jd->qtid[i] = seg[8 + 3 * i];
                    if(jd->qtid[i] > 3) return JDR_FMT3;
                }
                break;
            case 0xDD:
                if(len > JD_SZBUF) return JDR_MEM2;
                if(jd->infunc(jd, seg, len) != len) return JDR_INP;
                jd->nrst = LDB_WORD(seg);
                break;
            case 0xC4:
                if(len > JD_SZBUF) return JDR_MEM2;
                if(jd->infunc(jd, seg, len) != len) return JDR_INP;
                rc = create_huffman_tbl(jd, seg, len);
                if(rc) return rc;
                break;
            case 0xDB:
                if(len > JD_SZBUF) return JDR_MEM2;
                if(jd->infunc(jd, seg, len) != len) return JDR_INP;
                rc = create_qt_tbl(jd, seg, len);
                if(rc) return rc;
                break;
            case 0xDA:
                if(len > JD_SZBUF) return JDR_MEM2;
                if(jd->infunc(jd, seg, len) != len) return JDR_INP;
                if(!jd->width || !jd->height) return JDR_FMT1;
                if(seg[0] != jd->ncomp) return JDR_FMT3;
                for(i = 0; i < jd->ncomp; i++) {
                    b = seg[2 + 2 * i];
                    if(b != 0x00 && b != 0x11) return JDR_FMT3;
                    n = i ? 1 : 0;
                    if(!jd->huffbits[n][0] || !jd->huffbits[n][1]) return JDR_FMT1;
                    if(!jd->qttbl[jd->qtid[i]]) return JDR_FMT1;
                }
                n = jd->msy * jd->msx;
                if(!n) return JDR_FMT1;
                len = n * 64 * 2 + 64;
                if(len < 256) len = 256;
                jd->workbuf = alloc_pool(jd, len);
                if(!jd->workbuf) return JDR_MEM1;
                jd->mcubuf = alloc_pool(jd, (n + 2) * 64 * sizeof(jd_yuv_t));
                if(!jd->mcubuf) return JDR_MEM1;
                if(ofs %= JD_SZBUF) {
                    jd->dctr = jd->infunc(jd, seg + ofs, (size_t)(JD_SZBUF - ofs));
                }
                jd->dptr = seg + ofs - (JD_FASTDECODE ? 0 : 1);
                return JDR_OK;
            case 0xC1: case 0xC2: case 0xC3: case 0xC5: case 0xC6: case 0xC7:
            case 0xC9: case 0xCA: case 0xCB: case 0xCD: case 0xCE: case 0xCF:
            case 0xD9:
                return JDR_FMT3;
            default:
                if(jd->infunc(jd, 0, len) != len) return JDR_INP;
        }
    }
}

JRESULT jd_decomp(JDEC * jd, int (*outfunc)(JDEC *, void *, JRECT *), uint8_t scale) {
    unsigned int x, y, mx, my;
    uint16_t rst, rsc;
    JRESULT rc;

    if(scale > (JD_USE_SCALE ? 3 : 0)) return JDR_PAR;
    jd->scale = scale;

    mx = jd->msx * 8; my = jd->msy * 8;
    jd->dcv[2] = jd->dcv[1] = jd->dcv[0] = 0;
    rst = rsc = 0;

    rc = JDR_OK;
    for(y = 0; y < jd->height; y += my) {
        for(x = 0; x < jd->width; x += mx) {
            if(jd->nrst && rst++ == jd->nrst) {
                rc = jd_restart(jd, rsc++);
                if(rc != JDR_OK) return rc;
                rst = 1;
            }
            rc = jd_mcu_load(jd);
            if(rc != JDR_OK) return rc;
            rc = jd_mcu_output(jd, outfunc, x, y);
            if(rc != JDR_OK) return rc;
        }
    }
    return rc;
}
