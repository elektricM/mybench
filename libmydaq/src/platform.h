/* Tiny platform layer: mutex, condition variable, thread, monotonic clock, sleep.
 * Implemented in platform_posix.c (pthreads) and platform_win32.c (Win32). */
#ifndef LIBMYDAQ_PLATFORM_H
#define LIBMYDAQ_PLATFORM_H

#include <stdint.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
typedef CRITICAL_SECTION md_mutex;
typedef CONDITION_VARIABLE md_cond;
typedef HANDLE md_thread;
#else
#include <pthread.h>
typedef pthread_mutex_t md_mutex;
typedef pthread_cond_t md_cond;
typedef pthread_t md_thread;
#endif

int md_mutex_init(md_mutex *m);
void md_mutex_destroy(md_mutex *m);
void md_mutex_lock(md_mutex *m);
void md_mutex_unlock(md_mutex *m);

int md_cond_init(md_cond *c);
void md_cond_destroy(md_cond *c);
void md_cond_wait(md_cond *c, md_mutex *m);
/* Returns 0 when signalled (or spuriously woken), 1 on timeout. */
int md_cond_timedwait(md_cond *c, md_mutex *m, uint32_t timeout_ms);
void md_cond_signal(md_cond *c);
void md_cond_broadcast(md_cond *c);

typedef void (*md_thread_fn)(void *arg);
int md_thread_start(md_thread *t, md_thread_fn fn, void *arg);
void md_thread_join(md_thread t);

/* Monotonic time in milliseconds / microseconds (arbitrary origin). */
uint64_t md_now_ms(void);
uint64_t md_now_us(void);
void md_sleep_ms(uint32_t ms);

#endif
