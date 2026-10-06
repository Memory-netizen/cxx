#ifndef __STDMCHAR_H
#define __STDMCHAR_H

/* C2y (N3685) 7.26, text transcoding utilities.
 *
 * The version macro tracks the revision that introduced *this* header, not
 * the language mode in use: gcc's -std=c2y reports __STDC_VERSION__ as
 * 202500L while the headers it already had keep 202311L.
 */
#define __STDC_VERSION_STDMCHAR_H__ 202500L

#include <stddef.h>  /* size_t (7.22) */
#include <stdint.h>  /* uint32_t, and WCHAR_WIDTH's neighbours (7.23) */
#include <uchar.h>   /* char8_t, char16_t, char32_t (7.32) */
#include <wchar.h>   /* mbstate_t, wchar_t, WCHAR_MIN, WCHAR_MAX (7.33.1) */

/* 7.26.19: the four status codes a transcoding function returns. The type is
 * both an enumerated type and a typedef, so `stdc_mcerr` can be spelled
 * without `enum`. */
enum stdc_mcerr {
    stdc_mcerr_ok = 0,
    stdc_mcerr_invalid = -1,
    stdc_mcerr_incomplete_input = -2,
    stdc_mcerr_insufficient_output = -3,
};
typedef enum stdc_mcerr stdc_mcerr;

/* 7.26.16 and Table 7.8: the minimum maximum output of the single unit
 * conversion functions of each code unit type -- four code units for UTF-8,
 * two for UTF-16, one for UTF-32, and one for each of the two execution
 * encodings. They are minimums: a conversion may write fewer, never more. */
#define STDC_C8_MAX 4
#define STDC_C16_MAX 2
#define STDC_C32_MAX 1
#define STDC_MC_MAX 1
#define STDC_MWC_MAX 1

/* 7.26.16 also puts the <stdint.h> macros of 7.23 and the encoding macros of
 * 7.33.1 in scope here. The host headers may already define some of them, so
 * each is guarded.
 *
 * WCHAR_WIDTH is the width of wchar_t in bits, 32 on every target cxx has,
 * spelled as a literal so that it also works in #if.
 *
 * The six encoding macros are nonzero when the execution (MB_) or wide
 * execution (WCHAR_) encoding is that Unicode encoding and wchar_t is wide
 * enough for it. On this platform the narrow encoding is UTF-8 and the wide
 * one is UTF-32 with 32-bit wchar_t, so MB_UTF8 and WCHAR_UTF32 hold. The
 * transcoding functions below are written to match exactly that pair. */
#ifndef WCHAR_WIDTH
#define WCHAR_WIDTH 32
#endif

#ifndef WCHAR_UTF8
#define WCHAR_UTF8 0
#endif
#ifndef WCHAR_UTF16
#define WCHAR_UTF16 0
#endif
#ifndef WCHAR_UTF32
#define WCHAR_UTF32 1
#endif
#ifndef MB_UTF8
#define MB_UTF8 1
#endif
#ifndef MB_UTF16
#define MB_UTF16 0
#endif
#ifndef MB_UTF32
#define MB_UTF32 0
#endif

/* ---- 7.26.2 and 7.26.3: the transcoding functions --------------------
 *
 * The standard gives these external linkage. cxx ships no runtime library
 * of its own -- everything else in the library comes from the host -- so
 * they are static inline definitions here instead. For a program that
 * includes the header, which is how it gets their declarations anyway, that
 * is the same thing; a program that declares them itself without including
 * the header would not link.
 *
 * The encodings are the ones the macros above declare: the narrow execution
 * encoding is UTF-8, so `mc` and `c8` are one encoding, and the wide
 * execution encoding is UTF-32, so `mwc` and `c32` are one encoding too.
 * All three are stateless, so an mbstate_t object stays in the initial
 * conversion state throughout -- which is also why an incomplete sequence is
 * reported rather than remembered, as 7.26.2 allows: the function may not
 * consume it, and there is nowhere it would have to be kept.
 */

enum {
    STDC_MC_UTF8 = 0,  /* mc, c8 */
    STDC_MC_UTF16 = 1, /* c16 */
    STDC_MC_UTF32 = 2, /* mwc, c32 */
};

/* One code unit, as the unsigned value 7.26.1 NOTE 1 asks for. */
static inline uint32_t stdc_mc_get(const void *in, int enc, size_t i) {
    if (enc == STDC_MC_UTF8) return ((const unsigned char *)in)[i];
    if (enc == STDC_MC_UTF16) return ((const char16_t *)in)[i];
    return ((const uint32_t *)in)[i];
}

static inline void stdc_mc_put(void *out, int enc, size_t i, uint32_t u) {
    if (enc == STDC_MC_UTF8) {
        ((unsigned char *)out)[i] = (unsigned char)u;
    } else if (enc == STDC_MC_UTF16) {
        ((char16_t *)out)[i] = (char16_t)u;
    } else {
        ((uint32_t *)out)[i] = u;
    }
}

/* One indivisible unit of work, read: one code point taken from `n` code
 * units. Sets *used on success; an incomplete sequence is not consumed and
 * an invalid one has no specified state to leave behind. */
static inline stdc_mcerr stdc_mc_decode(int enc, const void *in, size_t n, char32_t *cp, size_t *used) {
    uint32_t u = stdc_mc_get(in, enc, 0);
    size_t need;
    uint32_t v;

    if (enc == STDC_MC_UTF32) {
        if (u > 0x10FFFF || (u >= 0xD800 && u <= 0xDFFF)) return stdc_mcerr_invalid;
        *cp = (char32_t)u;
        *used = 1;
        return stdc_mcerr_ok;
    }

    if (enc == STDC_MC_UTF16) {
        if (u < 0xD800 || u > 0xDFFF) {
            *cp = (char32_t)u;
            *used = 1;
            return stdc_mcerr_ok;
        }
        if (u >= 0xDC00) return stdc_mcerr_invalid; /* a low surrogate on its own */
        if (n < 2) return stdc_mcerr_incomplete_input;
        uint32_t lo = stdc_mc_get(in, enc, 1);
        if (lo < 0xDC00 || lo > 0xDFFF) return stdc_mcerr_invalid;
        *cp = (char32_t)(0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00));
        *used = 2;
        return stdc_mcerr_ok;
    }

    /* UTF-8. The lead byte decides how many code units the sequence needs,
     * and the continuation bytes are checked against it; the overlong
     * forms, the surrogates and the values past U+10FFFF are rejected once
     * the code point is assembled. */
    if (u < 0x80) {
        *cp = (char32_t)u;
        *used = 1;
        return stdc_mcerr_ok;
    }
    if (u >= 0xC2 && u <= 0xDF) {
        need = 2;
        v = u & 0x1F;
    } else if (u >= 0xE0 && u <= 0xEF) {
        need = 3;
        v = u & 0x0F;
    } else if (u >= 0xF0 && u <= 0xF4) {
        need = 4;
        v = u & 0x07;
    } else {
        return stdc_mcerr_invalid;
    }
    if (n < need) return stdc_mcerr_incomplete_input;
    for (size_t i = 1; i < need; i++) {
        uint32_t c = stdc_mc_get(in, enc, i);
        if ((c & 0xC0) != 0x80) return stdc_mcerr_invalid;
        v = (v << 6) | (c & 0x3F);
    }
    if (v < 0x80 || (v >= 0xD800 && v <= 0xDFFF) || v > 0x10FFFF) return stdc_mcerr_invalid;
    *cp = (char32_t)v;
    *used = need;
    return stdc_mcerr_ok;
}

/* One indivisible unit of work, write: how many code units `cp` takes. */
static inline size_t stdc_mc_len(int enc, char32_t cp) {
    if (enc == STDC_MC_UTF32) return 1;
    if (enc == STDC_MC_UTF16) return cp >= 0x10000 ? 2 : 1;
    return cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
}

static inline void stdc_mc_encode(int enc, char32_t cp, void *out, size_t len) {
    if (enc == STDC_MC_UTF32) {
        stdc_mc_put(out, enc, 0, cp);
        return;
    }
    if (enc == STDC_MC_UTF16) {
        if (len == 1) {
            stdc_mc_put(out, enc, 0, cp);
            return;
        }
        char32_t v = cp - 0x10000;
        stdc_mc_put(out, enc, 0, 0xD800 + (v >> 10));
        stdc_mc_put(out, enc, 1, 0xDC00 + (v & 0x3FF));
        return;
    }
    if (len == 1) {
        stdc_mc_put(out, enc, 0, cp);
    } else if (len == 2) {
        stdc_mc_put(out, enc, 0, 0xC0 | (cp >> 6));
        stdc_mc_put(out, enc, 1, 0x80 | (cp & 0x3F));
    } else if (len == 3) {
        stdc_mc_put(out, enc, 0, 0xE0 | (cp >> 12));
        stdc_mc_put(out, enc, 1, 0x80 | ((cp >> 6) & 0x3F));
        stdc_mc_put(out, enc, 2, 0x80 | (cp & 0x3F));
    } else {
        stdc_mc_put(out, enc, 0, 0xF0 | (cp >> 18));
        stdc_mc_put(out, enc, 1, 0x80 | ((cp >> 12) & 0x3F));
        stdc_mc_put(out, enc, 2, 0x80 | ((cp >> 6) & 0x3F));
        stdc_mc_put(out, enc, 3, 0x80 | (cp & 0x3F));
    }
}

/* One single unit conversion function (7.26.2).
 *
 * The counters move only when the whole indivisible unit of work is done,
 * which is what makes the two failure modes leave everything alone: too
 * little input to finish a sequence, or too little room to write one. */
#define STDC_MC_SINGLE(name, dstty, dstenc, srcty, srcenc)                                                        \
    static inline stdc_mcerr name(size_t *restrict output_size, dstty *restrict *restrict output,                 \
                                  size_t *restrict input_size, const srcty *restrict *restrict input,             \
                                  mbstate_t *restrict state) {                                                    \
        mbstate_t local = {0};                                                                                    \
        if (state == nullptr) state = &local;                                                                     \
        if (input == nullptr || *input == nullptr) {                                                              \
            *state = (mbstate_t){0};                                                                              \
            return stdc_mcerr_ok;                                                                                 \
        }                                                                                                         \
        if (input_size == nullptr || *input_size == 0) return stdc_mcerr_ok; /* empty input */                    \
        char32_t cp = 0;                                                                                          \
        size_t used = 0;                                                                                          \
        stdc_mcerr err = stdc_mc_decode(srcenc, *input, *input_size, &cp, &used);                                 \
        if (err != stdc_mcerr_ok) return err;                                                                     \
        size_t len = stdc_mc_len(dstenc, cp);                                                                     \
        if (output_size != nullptr && *output_size < len) return stdc_mcerr_insufficient_output;                  \
        if (output != nullptr && *output != nullptr) stdc_mc_encode(dstenc, cp, *output, len);                    \
        *input += used;                                                                                           \
        *input_size -= used;                                                                                      \
        if (output != nullptr && *output != nullptr) *output += len;                                              \
        if (output_size != nullptr) *output_size -= len;                                                          \
        return stdc_mcerr_ok;                                                                                     \
    }

/* One multi unit conversion function (7.26.3): the single unit function
 * applied until an error stops it or the input runs out. */
#define STDC_MC_MULTI(name, single, dstty, dstenc, srcty, srcenc)                                                 \
    static inline stdc_mcerr name(size_t *restrict output_size, dstty *restrict *restrict output,                 \
                                  size_t *restrict input_size, const srcty *restrict *restrict input,             \
                                  mbstate_t *restrict state) {                                                    \
        mbstate_t local = {0};                                                                                    \
        if (state == nullptr) state = &local;                                                                     \
        for (;;) {                                                                                                \
            stdc_mcerr err = single(output_size, output, input_size, input, state);                                \
            if (input == nullptr || *input == nullptr) return err;                                                \
            if (err != stdc_mcerr_ok) return err;                                                                 \
            if (input_size != nullptr && *input_size > 0) continue;                                               \
            if (mbsinit(state) == 0) continue;                                                                    \
            return err;                                                                                           \
        }                                                                                                         \
    }

/* mc: the narrow execution encoding, which is UTF-8. */
STDC_MC_SINGLE(stdc_mcnrtomcn, char, STDC_MC_UTF8, char, STDC_MC_UTF8)
STDC_MC_SINGLE(stdc_mcnrtomwcn, wchar_t, STDC_MC_UTF32, char, STDC_MC_UTF8)
STDC_MC_SINGLE(stdc_mcnrtoc8n, char8_t, STDC_MC_UTF8, char, STDC_MC_UTF8)
STDC_MC_SINGLE(stdc_mcnrtoc16n, char16_t, STDC_MC_UTF16, char, STDC_MC_UTF8)
STDC_MC_SINGLE(stdc_mcnrtoc32n, char32_t, STDC_MC_UTF32, char, STDC_MC_UTF8)

/* mwc: the wide execution encoding, which is UTF-32. */
STDC_MC_SINGLE(stdc_mwcnrtomcn, char, STDC_MC_UTF8, wchar_t, STDC_MC_UTF32)
STDC_MC_SINGLE(stdc_mwcnrtomwcn, wchar_t, STDC_MC_UTF32, wchar_t, STDC_MC_UTF32)
STDC_MC_SINGLE(stdc_mwcnrtoc8n, char8_t, STDC_MC_UTF8, wchar_t, STDC_MC_UTF32)
STDC_MC_SINGLE(stdc_mwcnrtoc16n, char16_t, STDC_MC_UTF16, wchar_t, STDC_MC_UTF32)
STDC_MC_SINGLE(stdc_mwcnrtoc32n, char32_t, STDC_MC_UTF32, wchar_t, STDC_MC_UTF32)

/* c8: UTF-8. */
STDC_MC_SINGLE(stdc_c8nrtomcn, char, STDC_MC_UTF8, char8_t, STDC_MC_UTF8)
STDC_MC_SINGLE(stdc_c8nrtomwcn, wchar_t, STDC_MC_UTF32, char8_t, STDC_MC_UTF8)
STDC_MC_SINGLE(stdc_c8nrtoc8n, char8_t, STDC_MC_UTF8, char8_t, STDC_MC_UTF8)
STDC_MC_SINGLE(stdc_c8nrtoc16n, char16_t, STDC_MC_UTF16, char8_t, STDC_MC_UTF8)
STDC_MC_SINGLE(stdc_c8nrtoc32n, char32_t, STDC_MC_UTF32, char8_t, STDC_MC_UTF8)

/* c16: UTF-16. */
STDC_MC_SINGLE(stdc_c16nrtomcn, char, STDC_MC_UTF8, char16_t, STDC_MC_UTF16)
STDC_MC_SINGLE(stdc_c16nrtomwcn, wchar_t, STDC_MC_UTF32, char16_t, STDC_MC_UTF16)
STDC_MC_SINGLE(stdc_c16nrtoc8n, char8_t, STDC_MC_UTF8, char16_t, STDC_MC_UTF16)
STDC_MC_SINGLE(stdc_c16nrtoc16n, char16_t, STDC_MC_UTF16, char16_t, STDC_MC_UTF16)
STDC_MC_SINGLE(stdc_c16nrtoc32n, char32_t, STDC_MC_UTF32, char16_t, STDC_MC_UTF16)

/* c32: UTF-32. */
STDC_MC_SINGLE(stdc_c32nrtomcn, char, STDC_MC_UTF8, char32_t, STDC_MC_UTF32)
STDC_MC_SINGLE(stdc_c32nrtomwcn, wchar_t, STDC_MC_UTF32, char32_t, STDC_MC_UTF32)
STDC_MC_SINGLE(stdc_c32nrtoc8n, char8_t, STDC_MC_UTF8, char32_t, STDC_MC_UTF32)
STDC_MC_SINGLE(stdc_c32nrtoc16n, char16_t, STDC_MC_UTF16, char32_t, STDC_MC_UTF32)
STDC_MC_SINGLE(stdc_c32nrtoc32n, char32_t, STDC_MC_UTF32, char32_t, STDC_MC_UTF32)

STDC_MC_MULTI(stdc_mcsnrtomcsn, stdc_mcnrtomcn, char, STDC_MC_UTF8, char, STDC_MC_UTF8)
STDC_MC_MULTI(stdc_mcsnrtomwcsn, stdc_mcnrtomwcn, wchar_t, STDC_MC_UTF32, char, STDC_MC_UTF8)
STDC_MC_MULTI(stdc_mcsnrtoc8sn, stdc_mcnrtoc8n, char8_t, STDC_MC_UTF8, char, STDC_MC_UTF8)
STDC_MC_MULTI(stdc_mcsnrtoc16sn, stdc_mcnrtoc16n, char16_t, STDC_MC_UTF16, char, STDC_MC_UTF8)
STDC_MC_MULTI(stdc_mcsnrtoc32sn, stdc_mcnrtoc32n, char32_t, STDC_MC_UTF32, char, STDC_MC_UTF8)

STDC_MC_MULTI(stdc_mwcsnrtomcsn, stdc_mwcnrtomcn, char, STDC_MC_UTF8, wchar_t, STDC_MC_UTF32)
STDC_MC_MULTI(stdc_mwcsnrtomwcsn, stdc_mwcnrtomwcn, wchar_t, STDC_MC_UTF32, wchar_t, STDC_MC_UTF32)
STDC_MC_MULTI(stdc_mwcsnrtoc8sn, stdc_mwcnrtoc8n, char8_t, STDC_MC_UTF8, wchar_t, STDC_MC_UTF32)
STDC_MC_MULTI(stdc_mwcsnrtoc16sn, stdc_mwcnrtoc16n, char16_t, STDC_MC_UTF16, wchar_t, STDC_MC_UTF32)
STDC_MC_MULTI(stdc_mwcsnrtoc32sn, stdc_mwcnrtoc32n, char32_t, STDC_MC_UTF32, wchar_t, STDC_MC_UTF32)

STDC_MC_MULTI(stdc_c8snrtomcsn, stdc_c8nrtomcn, char, STDC_MC_UTF8, char8_t, STDC_MC_UTF8)
STDC_MC_MULTI(stdc_c8snrtomwcsn, stdc_c8nrtomwcn, wchar_t, STDC_MC_UTF32, char8_t, STDC_MC_UTF8)
STDC_MC_MULTI(stdc_c8snrtoc8sn, stdc_c8nrtoc8n, char8_t, STDC_MC_UTF8, char8_t, STDC_MC_UTF8)
STDC_MC_MULTI(stdc_c8snrtoc16sn, stdc_c8nrtoc16n, char16_t, STDC_MC_UTF16, char8_t, STDC_MC_UTF8)
STDC_MC_MULTI(stdc_c8snrtoc32sn, stdc_c8nrtoc32n, char32_t, STDC_MC_UTF32, char8_t, STDC_MC_UTF8)

STDC_MC_MULTI(stdc_c16snrtomcsn, stdc_c16nrtomcn, char, STDC_MC_UTF8, char16_t, STDC_MC_UTF16)
STDC_MC_MULTI(stdc_c16snrtomwcsn, stdc_c16nrtomwcn, wchar_t, STDC_MC_UTF32, char16_t, STDC_MC_UTF16)
STDC_MC_MULTI(stdc_c16snrtoc8sn, stdc_c16nrtoc8n, char8_t, STDC_MC_UTF8, char16_t, STDC_MC_UTF16)
STDC_MC_MULTI(stdc_c16snrtoc16sn, stdc_c16nrtoc16n, char16_t, STDC_MC_UTF16, char16_t, STDC_MC_UTF16)
STDC_MC_MULTI(stdc_c16snrtoc32sn, stdc_c16nrtoc32n, char32_t, STDC_MC_UTF32, char16_t, STDC_MC_UTF16)

STDC_MC_MULTI(stdc_c32snrtomcsn, stdc_c32nrtomcn, char, STDC_MC_UTF8, char32_t, STDC_MC_UTF32)
STDC_MC_MULTI(stdc_c32snrtomwcsn, stdc_c32nrtomwcn, wchar_t, STDC_MC_UTF32, char32_t, STDC_MC_UTF32)
STDC_MC_MULTI(stdc_c32snrtoc8sn, stdc_c32nrtoc8n, char8_t, STDC_MC_UTF8, char32_t, STDC_MC_UTF32)
STDC_MC_MULTI(stdc_c32snrtoc16sn, stdc_c32nrtoc16n, char16_t, STDC_MC_UTF16, char32_t, STDC_MC_UTF32)
STDC_MC_MULTI(stdc_c32snrtoc32sn, stdc_c32nrtoc32n, char32_t, STDC_MC_UTF32, char32_t, STDC_MC_UTF32)

#endif
