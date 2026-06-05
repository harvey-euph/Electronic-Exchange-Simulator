#pragma once
#include <cstdint>

#if defined(__x86_64__) || defined(__i386__)
#include <x86intrin.h>
#endif

namespace Exchange {

inline uint64_t read_tsc_begin() {
#if defined(__x86_64__) || defined(__i386__)
    _mm_lfence();
    return __rdtsc();
#else
    return 0; 
#endif
}

inline uint64_t read_tsc_end() {
#if defined(__x86_64__) || defined(__i386__)
    unsigned aux = 0;
    const uint64_t tsc = __rdtscp(&aux);
    _mm_lfence();
    return tsc;
#else
    return 0;
#endif
}

} // namespace Exchange
