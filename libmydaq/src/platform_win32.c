/* Win32 implementation of the platform layer (Windows Vista or later). */
#include "platform.h"

#include <process.h>
#include <stdlib.h>

int md_mutex_init(md_mutex *m)
{
    InitializeCriticalSection(m);
    return 0;
}
void md_mutex_destroy(md_mutex *m) { DeleteCriticalSection(m); }
void md_mutex_lock(md_mutex *m) { EnterCriticalSection(m); }
void md_mutex_unlock(md_mutex *m) { LeaveCriticalSection(m); }

int md_cond_init(md_cond *c)
{
    InitializeConditionVariable(c);
    return 0;
}
void md_cond_destroy(md_cond *c) { (void)c; }
void md_cond_wait(md_cond *c, md_mutex *m) { SleepConditionVariableCS(c, m, INFINITE); }
void md_cond_signal(md_cond *c) { WakeConditionVariable(c); }
void md_cond_broadcast(md_cond *c) { WakeAllConditionVariable(c); }

int md_cond_timedwait(md_cond *c, md_mutex *m, uint32_t timeout_ms)
{
    if (SleepConditionVariableCS(c, m, (DWORD)timeout_ms))
        return 0;
    return GetLastError() == ERROR_TIMEOUT ? 1 : 0;
}

struct thread_start {
    md_thread_fn fn;
    void *arg;
};

static unsigned __stdcall thread_main(void *p)
{
    struct thread_start start = *(struct thread_start *)p;
    free(p);
    start.fn(start.arg);
    return 0;
}

int md_thread_start(md_thread *t, md_thread_fn fn, void *arg)
{
    struct thread_start *start = malloc(sizeof *start);
    uintptr_t h;
    if (!start)
        return -1;
    start->fn = fn;
    start->arg = arg;
    h = _beginthreadex(NULL, 0, thread_main, start, 0, NULL);
    if (h == 0) {
        free(start);
        return -1;
    }
    *t = (HANDLE)h;
    return 0;
}

void md_thread_join(md_thread t)
{
    WaitForSingleObject(t, INFINITE);
    CloseHandle(t);
}

uint64_t md_now_us(void)
{
    LARGE_INTEGER freq, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    return (uint64_t)((now.QuadPart / freq.QuadPart) * 1000000 +
                      (now.QuadPart % freq.QuadPart) * 1000000 / freq.QuadPart);
}

uint64_t md_now_ms(void) { return md_now_us() / 1000u; }
void md_sleep_ms(uint32_t ms) { Sleep((DWORD)ms); }
