#ifndef NEARCADE_ATOMIC_COMPAT_H
#define NEARCADE_ATOMIC_COMPAT_H

#ifdef _MSC_VER
#include <intrin.h>
typedef long nearcade_atomic_int;
static inline void nearcade_atomic_store(nearcade_atomic_int *ptr, nearcade_atomic_int val) {
    _InterlockedExchange(ptr, val);
}
static inline nearcade_atomic_int nearcade_atomic_load(nearcade_atomic_int *ptr) {
    _ReadWriteBarrier();
    return *ptr;
}
#else
#include <stdatomic.h>
typedef atomic_int nearcade_atomic_int;
static inline void nearcade_atomic_store(nearcade_atomic_int *ptr, nearcade_atomic_int val) {
    atomic_store(ptr, val);
}
static inline nearcade_atomic_int nearcade_atomic_load(nearcade_atomic_int *ptr) {
    return atomic_load(ptr);
}
#endif

#endif
