/* Minimal test harness for libmydaq (no dependencies). */
#ifndef LIBMYDAQ_TEST_H
#define LIBMYDAQ_TEST_H

#include "libmydaq/mydaq.h"

#include <stdio.h>
#include <string.h>

#ifndef MYDAQ_TEST_DATA_DIR
#define MYDAQ_TEST_DATA_DIR "data"
#endif

typedef struct test_case {
    const char *name;
    void (*fn)(void);
} test_case;

extern int test_failed;   /* set by the CHECK macros; reset per test */
void test_fail(const char *file, int line, const char *fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 3, 4)))
#endif
    ;

/* Path of a file in tests/data (static buffer, valid until the next call). */
const char *test_data(const char *name);

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            test_fail(__FILE__, __LINE__, "%s", #cond);                                            \
            return;                                                                                \
        }                                                                                          \
    } while (0)

#define CHECK_RC(expr)                                                                             \
    do {                                                                                           \
        const int rc_ = (expr);                                                                    \
        if (rc_ != MYDAQ_OK) {                                                                     \
            test_fail(__FILE__, __LINE__, "%s returned %s", #expr, mydaq_error_name(rc_));        \
            return;                                                                                \
        }                                                                                          \
    } while (0)

#define CHECK_ERR(expr, want)                                                                      \
    do {                                                                                           \
        const int rc_ = (expr);                                                                    \
        if (rc_ != (want)) {                                                                       \
            test_fail(__FILE__, __LINE__, "%s returned %s, expected %s", #expr,                    \
                      mydaq_error_name(rc_), mydaq_error_name(want));                              \
            return;                                                                                \
        }                                                                                          \
    } while (0)

#define CHECK_INT(a, b)                                                                            \
    do {                                                                                           \
        const long long a_ = (long long)(a), b_ = (long long)(b);                                  \
        if (a_ != b_) {                                                                            \
            test_fail(__FILE__, __LINE__, "%s == %lld, expected %lld", #a, a_, b_);                \
            return;                                                                                \
        }                                                                                          \
    } while (0)

/* Bit-exact double comparison (the calibration model must reproduce NI-DAQmx exactly). */
#define CHECK_EXACT(a, b)                                                                          \
    do {                                                                                           \
        const double a_ = (a), b_ = (b);                                                           \
        if (!(a_ == b_)) {                                                                         \
            test_fail(__FILE__, __LINE__, "%s == %.17g, expected %.17g", #a, a_, b_);              \
            return;                                                                                \
        }                                                                                          \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                                      \
    do {                                                                                           \
        const double a_ = (a), b_ = (b);                                                           \
        if (!(a_ - b_ <= (tol) && b_ - a_ <= (tol))) {                                             \
            test_fail(__FILE__, __LINE__, "%s == %.17g, expected %.17g", #a, a_, b_);              \
            return;                                                                                \
        }                                                                                          \
    } while (0)

extern const test_case unit_tests[];
extern const test_case replay_tests[];
extern const test_case emulator_tests[];

#endif
