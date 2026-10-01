#ifndef TEST_H
#define TEST_H

#include <stdio.h>

static int test_failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            test_failures++; \
        } \
    } while (0)

#define CHECK_EQ(actual, expected) \
    do { \
        long actual_ = (long)(actual); \
        long expected_ = (long)(expected); \
        if (actual_ != expected_) { \
            printf("FAIL %s:%d: %s is 0x%lX, expected 0x%lX\n", __FILE__, __LINE__, \
                   #actual, actual_, expected_); \
            test_failures++; \
        } \
    } while (0)

#define TEST_RESULT() (test_failures ? (printf("%d failure(s)\n", test_failures), 1) : (printf("ok\n"), 0))

#endif
