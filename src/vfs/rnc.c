/* Retail's NuPPUnpack (decomp/src/nucore/rnc.c) step for step, every read checked against the packed bytes and every
   write and back reference against the output. Its end of data (a zero chunk count) is the only stop retail has. */
#include "vfs/rnc.h"

#define NW_RNC_ID 0x524E4302u /* "RNC" 2 */

#define NW_RNC_IN(n)                                \
    do {                                            \
        if ((size_t)(in_end - in) < (size_t)(n)) { \
            return -1;                              \
        }                                           \
    } while (0)
#define NW_RNC_OUT(n)                                 \
    do {                                              \
        if ((size_t)(out_end - out) < (size_t)(n)) { \
            return -1;                                \
        }                                             \
    } while (0)

#define PP_NEXTBIT() \
    bits <<= 1;      \
    carry = bits >> 8; \
    carry &= 1
#define PP_REFILL()    \
    NW_RNC_IN(1);      \
    bits = *in++;      \
    bits <<= 1;        \
    bits += carry;     \
    carry = bits >> 8; \
    carry &= 1
#define PP_GETBIT(refill) \
    PP_NEXTBIT();         \
    bits &= 0xFF;         \
    if (bits == 0)        \
    goto refill

static uint32_t nw_rnc_be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

bool nw_rnc_header(const uint8_t *p, size_t size, uint32_t *unpacked, uint32_t *packed)
{
    if (size < NW_RNC_HEADER || nw_rnc_be32(p) != NW_RNC_ID) {
        return false;
    }
    *unpacked = nw_rnc_be32(p + 4);
    *packed = nw_rnc_be32(p + 8);
    return true;
}

int64_t nw_rnc_unpack(const uint8_t *in, const uint8_t *in_end, uint8_t *out, uint8_t *out_end)
{
    uint8_t *const start = out;
    const uint8_t *src = NULL;
    unsigned int bits = 0, carry = 1, len = 0, count = 0, high, even, odd, excess;

    PP_REFILL();
    bits <<= 1;
    goto next;

refill1:
    PP_REFILL();
    goto back1;
refill2:
    PP_REFILL();
    goto back2;
refill3:
    PP_REFILL();
    goto back3;
refill4:
    PP_REFILL();
    goto back4;
refill5:
    PP_REFILL();
    goto back5;
refill6:
    PP_REFILL();
    goto back6;
refill7:
    PP_REFILL();
    goto back7;
refill8:
    PP_REFILL();
    goto back8;

raw3:
    count = 3;
raw:
    PP_GETBIT(refill1);
back1:
    len <<= 1;
    len += carry;
    if ((int)--count >= 0) {
        goto raw;
    }
    len += 2;
    NW_RNC_IN(4 * ((size_t)len + 1));
    NW_RNC_OUT(4 * ((size_t)len + 1));
    do {
        even = in[0];
        odd = in[1];
        out[0] = (uint8_t)even;
        out[1] = (uint8_t)odd;
        even = in[2];
        odd = in[3];
        out[2] = (uint8_t)even;
        out[3] = (uint8_t)odd;
        in += 4;
        out += 4;
    } while ((int)--len >= 0);
    goto next;

getlen:
    PP_GETBIT(refill2);
back2:
    count <<= 1;
    count += carry;
    PP_GETBIT(refill3);
back3:
    if (!carry) {
        goto getoffs_hi;
    }
    count--;
    PP_GETBIT(refill4);
back4:
    count <<= 1;
    count += carry;
    excess = count - 9;
    if (excess == 0) {
        goto raw3;
    }
getoffs_hi:
    PP_GETBIT(refill5);
back5:
    if (!carry) {
        goto getoffs;
    }
    PP_GETBIT(refill6);
back6:
    len <<= 1;
    len += carry;
    PP_GETBIT(refill7);
back7:
    if (carry) {
        goto bigoffs;
    }
    if (len != 0) {
        goto swap;
    }
    len = 1;
getoffs_lo:
    PP_GETBIT(refill8);
back8:
    len <<= 1;
    len += carry;
swap:
    high = (len << 8) & 0xFF00;
    len >>= 8;
    len |= high;
getoffs:
    high = len & 0xFF00;
    carry = count & 1;
    NW_RNC_IN(1);
    len = *in++;
    len |= high;
    count >>= 1;
    if ((size_t)(out - start) < (size_t)len + 1) {
        return -1;
    }
    src = out - len - 1;
    if (carry) {
        NW_RNC_OUT(1);
        *out++ = *src++;
    }
    count--;
    if (len == 0) {
        len = *src;
        do {
            NW_RNC_OUT(2);
            out[0] = (uint8_t)len;
            out[1] = (uint8_t)len;
            out += 2;
        } while ((int)--count >= 0);
        goto next;
    }
pairs:
    NW_RNC_OUT(2);
    odd = src[1];
    even = src[0];
    out[1] = (uint8_t)odd;
    out[0] = (uint8_t)even;
    src += 2;
    out += 2;
    if ((int)--count >= 0) {
        goto pairs;
    }
    goto next;

refill0:
    PP_REFILL();
    if (carry) {
        goto copy;
    }
literal:
    NW_RNC_IN(1);
    NW_RNC_OUT(1);
    *out++ = *in++;
next:
    PP_NEXTBIT();
    if (!carry) {
        NW_RNC_IN(1);
        NW_RNC_OUT(1);
        *out++ = *in++;
        PP_NEXTBIT();
        if (!carry) {
            goto literal;
        }
    }
    bits &= 0xFF;
    if (bits == 0) {
        goto refill0;
    }
copy:
    count = 2;
    len = 0;
    PP_GETBIT(refill9);
back9:
    if (!carry) {
        goto getlen;
    }
    PP_GETBIT(refill10);
back10:
    if (!carry) {
        goto getoffs;
    }
    count = 3;
    PP_GETBIT(refill11);
back11:
    if (!carry) {
        goto getoffs_hi;
    }
    NW_RNC_IN(1);
    count = *in++;
    if (count == 0) {
        goto chunkend;
    }
    count += 8;
    goto getoffs_hi;
bigoffs:
    PP_GETBIT(refill12);
back12:
    len <<= 1;
    len += carry;
    len |= 4;
    PP_GETBIT(refill13);
back13:
    if (carry) {
        goto swap;
    }
    goto getoffs_lo;

refill9:
    PP_REFILL();
    goto back9;
refill10:
    PP_REFILL();
    goto back10;
refill11:
    PP_REFILL();
    goto back11;
refill12:
    PP_REFILL();
    goto back12;
refill13:
    PP_REFILL();
    goto back13;

chunkend:
    PP_NEXTBIT();
    bits &= 0xFF;
    if (bits == 0) {
        PP_REFILL();
    }
    if (carry) {
        goto next;
    }
    return out - start;
}
