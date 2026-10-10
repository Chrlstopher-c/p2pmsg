/* Mini framework de test header-only. Usage : RUN(fn) dans main, puis return TEST_SUMMARY(). */
#ifndef P2P_TEST_H
#define P2P_TEST_H

#include <stdio.h>
#include <string.h>

static inline int *test_fail_counter(void)
{
    static int count = 0;
    return &count;
}

static inline int test_summary(void)
{
    int failed = *test_fail_counter();
    fprintf(stdout, "%s: %d échec(s)\n", failed ? "FAIL" : "PASS", failed);
    return failed ? 1 : 0;
}

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "%s:%d: CHECK(%s) échoué\n", __FILE__, __LINE__, #cond); \
        ++*test_fail_counter(); \
    } \
} while (0)

#define CHECK_EQ_INT(a, b) do { \
    long long test_a_ = (long long)(a); \
    long long test_b_ = (long long)(b); \
    if (test_a_ != test_b_) { \
        fprintf(stderr, "%s:%d: CHECK_EQ_INT(%s, %s) : %lld != %lld\n", \
                __FILE__, __LINE__, #a, #b, test_a_, test_b_); \
        ++*test_fail_counter(); \
    } \
} while (0)

#define CHECK_EQ_MEM(a, b, n) do { \
    const void *test_pa_ = (a); \
    const void *test_pb_ = (b); \
    size_t test_n_ = (size_t)(n); \
    if (test_n_ > 0 && memcmp(test_pa_, test_pb_, test_n_) != 0) { \
        fprintf(stderr, "%s:%d: CHECK_EQ_MEM(%s, %s, %s) : contenus différents\n", \
                __FILE__, __LINE__, #a, #b, #n); \
        ++*test_fail_counter(); \
    } \
} while (0)

#define RUN(fn) do { \
    int test_before_ = *test_fail_counter(); \
    fn(); \
    fprintf(stdout, "%s %s\n", *test_fail_counter() == test_before_ ? "OK  " : "FAIL", #fn); \
} while (0)

#endif
