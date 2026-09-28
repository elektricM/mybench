/* Test runner: `libmydaq_tests [name-prefix]` runs the matching tests (all by default). */
#include "test.h"

#include <stdarg.h>

int test_failed;

void test_fail(const char *file, int line, const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "    FAIL %s:%d: ", file, line);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    test_failed = 1;
}

const char *test_data(const char *name)
{
    static char path[1024];
    snprintf(path, sizeof path, "%s/%s", MYDAQ_TEST_DATA_DIR, name);
    return path;
}

static int run(const test_case *tests, const char *prefix, int *count)
{
    int failures = 0;
    for (; tests->name; ++tests) {
        if (prefix && strncmp(tests->name, prefix, strlen(prefix)) != 0)
            continue;
        test_failed = 0;
        tests->fn();
        ++*count;
        printf("%s %s\n", test_failed ? "FAIL" : "ok  ", tests->name);
        fflush(stdout);
        failures += test_failed;
    }
    return failures;
}

int main(int argc, char **argv)
{
    const char *prefix = argc > 1 ? argv[1] : NULL;
    int count = 0, failures = 0;
    failures += run(unit_tests, prefix, &count);
    failures += run(replay_tests, prefix, &count);
    failures += run(emulator_tests, prefix, &count);
    printf("%d test(s), %d failure(s)\n", count, failures);
    if (count == 0) {
        fprintf(stderr, "no test matches '%s'\n", prefix ? prefix : "");
        return 2;
    }
    return failures ? 1 : 0;
}
