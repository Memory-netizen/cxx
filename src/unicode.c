#include "cxx.h"
#include "width_property.h"
#include "xid_property.h"

// Encode a given character in UTF-8.
int encode_utf8(char *buf, uint32_t c) {
    if (0xD800 <= c && c <= 0xDFFF) return 0;
    if (c > 0x10FFFF) return 0;

    if (c <= 0x7F) {
        buf[0] = c;
        return 1;
    }

    if (c <= 0x7FF) {
        buf[0] = 0b11000000 | (c >> 6);
        buf[1] = 0b10000000 | (c & 0b00111111);
        return 2;
    }

    if (c <= 0xFFFF) {
        buf[0] = 0b11100000 | (c >> 12);
        buf[1] = 0b10000000 | ((c >> 6) & 0b00111111);
        buf[2] = 0b10000000 | (c & 0b00111111);
        return 3;
    }

    buf[0] = 0b11110000 | (c >> 18);
    buf[1] = 0b10000000 | ((c >> 12) & 0b00111111);
    buf[2] = 0b10000000 | ((c >> 6) & 0b00111111);
    buf[3] = 0b10000000 | (c & 0b00111111);
    return 4;
}

// Read a UTF-8-encoded Unicode code point from a source file.
// We assume that source files are always in UTF-8.
//
// UTF-8 is a variable-width encoding in which one code point is
// encoded in one to four bytes. One byte UTF-8 code points are
// identical to ASCII. Non-ASCII characters are encoded using more
// than one byte.
// *success must initialize as false
uint32_t decode_utf8(char **new_pos, char *p, bool *success) {
    unsigned char b = *p;
    if (b <= 0x7F) {
        *new_pos = p + 1;
        *success = true;
        return b;
    }
    if (b > 0xF4) return 0xFFFD;

    int len;
    uint32_t c;

    if (b >= 0b11110000) {
        len = 4;
        c = b & 0b111;
    } else if (b >= 0b11100000) {
        len = 3;
        c = b & 0b1111;
    } else if (b >= 0b11000000) {
        len = 2;
        c = b & 0b11111;
    } else {
        return 0xFFFD;
    }

    for (int i = 1; i < len; i++) {
        unsigned char next = p[i];
        if ((next & 0b11000000) != 0b10000000) return 0xFFFD;
        c = (c << 6) | (next & 0b111111);
    }

    if ((len == 2 && c < 0x80) || (len == 3 && c < 0x800) || (len == 4 && c < 0x10000)) return 0xFFFD;

    if (0xD800 <= c && c <= 0xDFFF) return 0xFFFD;
    if (c > 0x10FFFF) return 0xFFFD;

    *new_pos = p + len;
    *success = true;
    return c;
}

static bool in_range(uint32_t *range, size_t num_intervals, uint32_t c) {
    int lo = 0, hi = (int)num_intervals - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        uint32_t start = range[mid * 2];
        uint32_t end = range[mid * 2 + 1];
        if (c < start)
            hi = mid - 1;
        else if (c > end)
            lo = mid + 1;
        else
            return true;
    }
    return false;
}

// Returns true if a given character is acceptable as
// the first character of an identifier.
bool is_ident1(uint32_t c) {
    if ('a' <= c && c <= 'z') return true;
    if ('A' <= c && c <= 'Z') return true;
    if (c == '_' || c == '$') return true;
    return in_range(xid_start, XID_START_LEN, c);
}

// Returns true if a given character is acceptable as
// a non-first character of an identifier.
bool is_ident2(uint32_t c) {
    if ('a' <= c && c <= 'z') return true;
    if ('A' <= c && c <= 'Z') return true;
    if (c == '_' || c == '$') return true;
    if ('0' <= c && c <= '9') return true;
    return in_range(xid_continue, XID_CONTINUE_LEN, c);
}

// Returns the number of columns needed to display a given
// character in a fixed-width font.
static int char_width(uint32_t c) {
    if (in_range(zero_width, ZERO_WIDTH_LEN, c)) return 0;
    if (in_range(double_width, DOUBLE_WIDTH_LEN, c)) return 2;
    return 1;
}

// Returns the number of columns needed to display a given
// string in a fixed-width font.
int display_width(char *p, int len) {
    char *start = p;
    int w = 0;
    while (p - start < len) {
        bool success = false;
        uint32_t c = decode_utf8(&p, p, &success);
        w += char_width(c);
    }
    return w;
}
