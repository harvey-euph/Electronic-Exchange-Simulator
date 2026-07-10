#pragma once
#include <cstdint>

#if defined(__x86_64__) || defined(__i386__)

#include <x86intrin.h>
namespace Exchange {
inline uint64_t read_tsc_begin() 
{
    _mm_lfence();
    return __rdtsc();
}

inline uint64_t read_tsc_end()
{
    unsigned aux = 0;
    const uint64_t tsc = __rdtscp(&aux);
    _mm_lfence();
    return tsc;
}
} // namespace Exchange

#else

namespace Exchange {

inline uint64_t read_tsc_begin() { return 0; }
inline uint64_t read_tsc_end() { return 0; }

} // namespace Exchange

#endif

