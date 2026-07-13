#ifndef NEARCADE_THREAD_COMPAT_H
#define NEARCADE_THREAD_COMPAT_H

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef struct {
    SRWLOCK lock;
} nearcade_mutex_t;

#define NEARCADE_MUTEX_INITIALIZER { SRWLOCK_INIT }

static inline int nearcade_mutex_init(nearcade_mutex_t *m) {
    InitializeSRWLock(&m->lock);
    return 0;
}

static inline int nearcade_mutex_destroy(nearcade_mutex_t *m) {
    (void)m;
    return 0;
}

static inline int nearcade_mutex_lock(nearcade_mutex_t *m) {
    AcquireSRWLockExclusive(&m->lock);
    return 0;
}

static inline int nearcade_mutex_unlock(nearcade_mutex_t *m) {
    ReleaseSRWLockExclusive(&m->lock);
    return 0;
}

typedef struct {
    HANDLE handle;
    void *(*func)(void*);
    void *arg;
} nearcade_thread_t;

static DWORD WINAPI nearcade_thread_trampoline(LPVOID p) {
    nearcade_thread_t *t = (nearcade_thread_t*)p;
    t->func(t->arg);
    return 0;
}

static inline int nearcade_thread_create(nearcade_thread_t *t, void *(*start)(void*), void *arg) {
    t->func = start;
    t->arg = arg;
    t->handle = CreateThread(NULL, 0, nearcade_thread_trampoline, t, 0, NULL);
    return t->handle ? 0 : -1;
}

#else
#include <pthread.h>

typedef pthread_mutex_t nearcade_mutex_t;
#define NEARCADE_MUTEX_INITIALIZER PTHREAD_MUTEX_INITIALIZER

static inline int nearcade_mutex_init(nearcade_mutex_t *m) {
    return pthread_mutex_init(m, NULL);
}

static inline int nearcade_mutex_destroy(nearcade_mutex_t *m) {
    return pthread_mutex_destroy(m);
}

static inline int nearcade_mutex_lock(nearcade_mutex_t *m) {
    return pthread_mutex_lock(m);
}

static inline int nearcade_mutex_unlock(nearcade_mutex_t *m) {
    return pthread_mutex_unlock(m);
}

typedef pthread_t nearcade_thread_t;

static inline int nearcade_thread_create(nearcade_thread_t *t, void *(*start)(void*), void *arg) {
    return pthread_create(t, NULL, start, arg);
}
#endif

#endif
