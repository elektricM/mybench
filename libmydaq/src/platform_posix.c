/* POSIX (pthreads) implementation of the platform layer. Linux, macOS, BSD. */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#else
#define _POSIX_C_SOURCE 200809L
#endif

#include "platform.h"

#include <errno.h>
#include <stdlib.h>
#include <time.h>

int md_mutex_init(md_mutex *m) { return pthread_mutex_init(m, NULL) == 0 ? 0 : -1; }
void md_mutex_destroy(md_mutex *m) { pthread_mutex_destroy(m); }
void md_mutex_lock(md_mutex *m) { pthread_mutex_lock(m); }
void md_mutex_unlock(md_mutex *m) { pthread_mutex_unlock(m); }

int md_cond_init(md_cond *c)
{
#if defined(__APPLE__)
    return pthread_cond_init(c, NULL) == 0 ? 0 : -1;   /* timed waits use the relative variant */
#else
    pthread_condattr_t attr;
    int rc;
    if (pthread_condattr_init(&attr) != 0)
        return -1;
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    rc = pthread_cond_init(c, &attr);
    pthread_condattr_destroy(&attr);
    return rc == 0 ? 0 : -1;
#endif
}

void md_cond_destroy(md_cond *c) { pthread_cond_destroy(c); }
void md_cond_wait(md_cond *c, md_mutex *m) { pthread_cond_wait(c, m); }
void md_cond_signal(md_cond *c) { pthread_cond_signal(c); }
void md_cond_broadcast(md_cond *c) { pthread_cond_broadcast(c); }

int md_cond_timedwait(md_cond *c, md_mutex *m, uint32_t timeout_ms)
{
    struct timespec ts;
    int rc;
#if defined(__APPLE__)
    ts.tv_sec = (time_t)(timeout_ms / 1000u);
    ts.tv_nsec = (long)(timeout_ms % 1000u) * 1000000L;
    rc = pthread_cond_timedwait_relative_np(c, m, &ts);
#else
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ts.tv_sec += (time_t)(timeout_ms / 1000u);
    ts.tv_nsec += (long)(timeout_ms % 1000u) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000L;
    }
    rc = pthread_cond_timedwait(c, m, &ts);
#endif
    return rc == ETIMEDOUT ? 1 : 0;
}

struct thread_start {
    md_thread_fn fn;
    void *arg;
};

static void *thread_main(void *p)
{
    struct thread_start start = *(struct thread_start *)p;
    free(p);
    start.fn(start.arg);
    return NULL;
}

int md_thread_start(md_thread *t, md_thread_fn fn, void *arg)
{
    struct thread_start *start = malloc(sizeof *start);
    if (!start)
        return -1;
    start->fn = fn;
    start->arg = arg;
    if (pthread_create(t, NULL, thread_main, start) != 0) {
        free(start);
        return -1;
    }
    return 0;
}

void md_thread_join(md_thread t) { pthread_join(t, NULL); }

uint64_t md_now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

uint64_t md_now_ms(void) { return md_now_us() / 1000u; }

void md_sleep_ms(uint32_t ms)
{
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
    }
}
