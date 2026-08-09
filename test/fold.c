// Constant folding test — builtin types only, no headers needed.
int printf(char *fmt, ...);

static int failed;
static int test_no;

#define CHECK_EQ(name, expr, expected)                                                \
    do {                                                                              \
        test_no++;                                                                    \
        long long val = (long long)(expr);                                            \
        long long exp = (long long)(expected);                                        \
        if (val != exp) {                                                             \
            printf("FAIL %d %-30s => %lld expected %lld\n", test_no, name, val, exp); \
            failed++;                                                                 \
        }                                                                             \
    } while (0)

#define CHECK_FLT(name, expr, expected)                                           \
    do {                                                                          \
        test_no++;                                                                \
        double val = (double)(expr);                                              \
        double exp = (double)(expected);                                          \
        double diff = val - exp;                                                  \
        if (diff < 0) diff = -diff;                                               \
        if (!(val != val && exp != exp) && diff > 1e-10) {                        \
            printf("FAIL %d %-30s => %f expected %f\n", test_no, name, val, exp); \
            failed++;                                                             \
        }                                                                         \
    } while (0)

#define CHECK_NAN(name, expr)                                                 \
    do {                                                                      \
        test_no++;                                                            \
        double val = (double)(expr);                                          \
        if (val == val) {                                                     \
            printf("FAIL %d %-30s => %f expected NaN\n", test_no, name, val); \
            failed++;                                                         \
        }                                                                     \
    } while (0)

#define CHECK_TRUE(name, expr)                                 \
    do {                                                       \
        test_no++;                                             \
        if (!((long long)(expr))) {                            \
            printf("FAIL %d %-30s => false\n", test_no, name); \
            failed++;                                          \
        }                                                      \
    } while (0)

#define CHECK_FALSE(name, expr)                               \
    do {                                                      \
        test_no++;                                            \
        if ((long long)(expr)) {                              \
            printf("FAIL %d %-30s => true\n", test_no, name); \
            failed++;                                         \
        }                                                     \
    } while (0)

int main() {
    // === 1. Integer arithmetic ===
    CHECK_EQ("1+2", 1 + 2, 3);
    CHECK_EQ("100-40", 100 - 40, 60);
    CHECK_EQ("6*7", 6 * 7, 42);
    CHECK_EQ("20/3", 20 / 3, 6);
    CHECK_EQ("(-10)/3", (-10) / 3, -3);
    CHECK_EQ("20%3", 20 % 3, 2);
    CHECK_EQ("(-10)%3", (-10) % 3, -1);
    CHECK_EQ("3&5", 3 & 5, 1);
    CHECK_EQ("3|5", 3 | 5, 7);
    CHECK_EQ("3^5", 3 ^ 5, 6);
    CHECK_EQ("1<<10", 1 << 10, 1024);
    CHECK_EQ("1024U>>3", 1024U >> 3, 128U);
    CHECK_EQ("(unsigned)(-1)>>30", (unsigned)(-1) >> 30, 3U);

    // === 2. Signed/unsigned ===
    CHECK_EQ("1U+2U", 1U + 2U, 3U);
    CHECK_EQ("0U-1U", 0U - 1U, (unsigned)-1);
    CHECK_EQ("5U*3U", 5U * 3U, 15U);
    CHECK_EQ("10U/3U", 10U / 3U, 3U);
    CHECK_EQ("-5&3", -5 & 3, 3);
    CHECK_EQ("1ULL<<63", 1ULL << 63, 9223372036854775808ULL);
    CHECK_EQ("~0U", ~0U, (unsigned)-1);
    CHECK_EQ("-(unsigned)1", -(unsigned)1, (unsigned)-1);

    // === 3. Comparison ===
    CHECK_EQ("1==1", 1 == 1, 1);
    CHECK_EQ("1!=2", 1 != 2, 1);
    CHECK_EQ("5==5", 5 == 5, 1);
    CHECK_EQ("3<5", 3 < 5, 1);
    CHECK_EQ("5>=5", 5 >= 5, 1);
    CHECK_EQ("(unsigned)-1>0", (unsigned)-1 > 0, 1);
    CHECK_EQ("(unsigned)1<-1", (unsigned)1 < -1, 1);  // -1 becomes UINT_MAX
    CHECK_EQ("0LL-1LL<0LL", 0LL - 1LL < 0LL, 1);

    // === 4. Logical and unary ===
    CHECK_EQ("0&&1", 0 && 1, 0);
    CHECK_EQ("1&&2", 1 && 2, 1);
    CHECK_EQ("0||5", 0 || 5, 1);
    CHECK_EQ("1||5", 1 || 5, 1);
    CHECK_EQ("!0", !0, 1);
    CHECK_EQ("!5", !5, 0);
    CHECK_EQ("~0", ~0, -1);
    CHECK_EQ("~0xFF", ~0xFF, -256);
    CHECK_EQ("+5", +5, 5);
    CHECK_EQ("-5", -5, -5);

    // === 5. Cast ===
    CHECK_EQ("(char)256", (char)256, 0);
    CHECK_EQ("(signed char)128", (signed char)128, -128);
    CHECK_EQ("(unsigned char)(-1)", (unsigned char)(-1), 255);
    CHECK_EQ("(short)65535", (short)65535, -1);
    CHECK_EQ("(int)3.14", (int)3.14, 3);
    CHECK_EQ("(_Bool)5", (_Bool)5, 1);
    CHECK_EQ("(_Bool)0", (_Bool)0, 0);
    CHECK_EQ("(long long)(int)-1", (long long)(int)-1, -1LL);

    // === 6. Shift boundaries ===
    CHECK_EQ("1U<<0", 1U << 0, 1U);
    CHECK_EQ("1U<<1", 1U << 1, 2U);
    CHECK_EQ("1U<<30", 1U << 30, 1073741824U);
    CHECK_EQ("1U<<31", 1U << 31, 2147483648U);
    CHECK_EQ("1ULL<<63", 1ULL << 63, 9223372036854775808ULL);
    CHECK_EQ("0xFFU>>4", 0xFFU >> 4, 15U);
    CHECK_EQ("0U>>1", 0U >> 1, 0U);

    // === 7. Overflow/wrap-around ===
    CHECK_EQ("2147483647+1", 2147483647 + 1, -2147483648);
    CHECK_EQ("4294967295U+1U", 4294967295U + 1U, 0U);
    CHECK_EQ("9223372036854775807LL+1", 9223372036854775807LL + 1LL, -9223372036854775807LL - 1LL);
    CHECK_EQ("(int)2147483648U", (int)2147483648U, -2147483648);
    CHECK_EQ("(uchr)(255+1)", (unsigned char)(255 + 1), 0);

    // === 8. Conditional ===
    CHECK_EQ("1?10:20", 1 ? 10 : 20, 10);
    CHECK_EQ("0?10:20", 0 ? 10 : 20, 20);
    CHECK_EQ("(1&&0)?5:3", (1 && 0) ? 5 : 3, 3);
    CHECK_EQ("(5>3)?100:0", (5 > 3) ? 100 : 0, 100);
    CHECK_EQ("(0||1)?7:9", (0 || 1) ? 7 : 9, 7);

    // === 9. Floating point ===
    CHECK_FLT("1.0+2.0", 1.0 + 2.0, 3.0);
    CHECK_FLT("3.0*4.0", 3.0 * 4.0, 12.0);
    CHECK_FLT("10.0/3.0", 10.0 / 3.0, 3.3333333333333335);
    CHECK_FLT("-3.14", -3.14, -3.14);
    CHECK_FLT("0.1+0.2", 0.1 + 0.2, 0.30000000000000004);
    CHECK_FLT("(double)3", (double)3, 3.0);
    CHECK_EQ("(int)3.14", (int)3.14, 3);
    CHECK_FLT("(float)3.14", (float)3.14, 3.14f);

    // === 10. NaN / Inf ===
    CHECK_NAN("0.0/0.0", 0.0 / 0.0);
    CHECK_TRUE("1.0/0.0 is inf", 1.0 / 0.0 > 1e308);
    CHECK_TRUE("(-1.0/0.0) is -inf", (-1.0 / 0.0) < -1e308);
    CHECK_FALSE("NaN==NaN", (0.0 / 0.0) == (0.0 / 0.0));

    // === 11. Deep nesting ===
    CHECK_EQ("(1+2)*(3+4)/5", (1 + 2) * (3 + 4) / 5, 4);
    CHECK_EQ("~(-1)&0xFF", ~(-1) & 0xFF, 0);
    CHECK_FALSE("!!(5>3)&&(2<1)", !!(5 > 3) && (2 < 1));
    CHECK_EQ("(int)((char)(256+1))", (int)((char)(256 + 1)), 1);
    CHECK_EQ("-(uchr)(200+56)", -(unsigned char)(200 + 56), 0);
    CHECK_EQ("(uns)((schr)(-1))", (unsigned)((signed char)(-1)), (unsigned)-1);
    CHECK_EQ("1+2+3+4+5+6+7+8+9+10", 1 + 2 + 3 + 4 + 5 + 6 + 7 + 8 + 9 + 10, 55);

    // === 12. Control flow ===
    {
        int x;
        if (1)
            x = 5;
        else
            x = 3;
        CHECK_EQ("if(1)=5", x, 5);
    }
    {
        int x = 3;
        while (0) x = 5;
        CHECK_EQ("while(0)=3", x, 3);
    }
    {
        int x = 3;
        for (; 0;) x = 5;
        CHECK_EQ("for(;0;)=3", x, 3);
    }
    {
        int x = 0;
        switch (2) {
            case 1 + 1:
                x = 5;
                break;
            default:
                x = 3;
                break;
        }
        CHECK_EQ("switch(1+1)=5", x, 5);
    }

    // === 13. sizeof ===
    CHECK_EQ("sizeof(int)+1", sizeof(int) + 1, 5);
    CHECK_EQ("sizeof(char[3])*2", sizeof(char[3]) * 2, 6);
    CHECK_EQ("sizeof(int)==4?1:0", sizeof(int) == 4 ? 1 : 0, 1);

    // === 14. Special values ===
    CHECK_EQ("0+0", 0 + 0, 0);
    CHECK_EQ("0*99999", 0 * 99999, 0);
    CHECK_EQ("-1+1", -1 + 1, 0);
    CHECK_EQ("0/-1", 0 / -1, 0);

    if (failed == 0)
        printf("OK (%d tests)\n", test_no);
    else
        printf("%d FAILURES / %d tests\n", failed, test_no);
    return failed;
}
