#ifndef TEST_QWRT_H
#define TEST_QWRT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)

#define ASSERT(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "  FAIL: %s (line %d)\n", #expr, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_STR_EQ(a, b) do { \
    if (strcmp((a), (b)) != 0) { \
        fprintf(stderr, "  FAIL: \"%s\" != \"%s\" (line %d)\n", (a), (b), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_NULL(p) ASSERT((p) == NULL)
#define ASSERT_NOT_NULL(p) ASSERT((p) != NULL)
#define ASSERT_EQ(a, b) ASSERT((a) == (b))

#define RUN_TEST(name) do { \
    int prev_failed = tests_failed; \
    printf("  %s...", #name); \
    tests_run++; \
    test_##name(); \
    if (tests_failed == prev_failed) { \
        printf(" PASS\n"); \
        tests_passed++; \
    } else { \
        printf("\n"); \
    } \
} while(0)

#endif /* TEST_QWRT_H */
