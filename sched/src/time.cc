#include "time.h"
#include "atomic.h"

#include <sys/time.h>
#include <time.h>

#if defined __APPLE__
#include <mach/mach_time.h>
#endif


static inline __attribute__((always_inline))
uint64_t _nanotime(void) {
#if defined(__MACH__)
  static mach_timebase_info_data_t ti;
  static sync_once_flag once = 0;
  sync_once(&once, {
    mach_timebase_info(&ti);
  });
  uint64_t t = mach_absolute_time();
  return (t * ti.numer) / ti.denom;
#elif defined(CLOCK_MONOTONIC)
  struct timespec ts;
  #ifdef NDEBUG
  clock_gettime(CLOCK_MONOTONIC, &ts);
  #else
  assert(clock_gettime(CLOCK_MONOTONIC, &ts) == 0);
  #endif
  return (uint64_t(ts.tv_sec) * 1000000000) + ts.tv_nsec;
// #elif (defined _MSC_VER && (defined _M_IX86 || defined _M_X64))
  // TODO: QueryPerformanceCounter
#else
  struct timeval tv;
  #ifdef NDEBUG
  gettimeofday(&tv, nullptr);
  #else
  assert(gettimeofday(&tv, nullptr) == 0);
  #endif
  return (uint64_t(tv.tv_sec) * 1000000000) + (uint64_t(tv.tv_usec) * 1000);
#endif
}

// #define USE_RDTSC_CACHE

uint64_t nanotime(void) {

#ifdef USE_RDTSC_CACHE
  #if (defined(__GNUC__) || defined(__clang__)) && \
      (defined(__i386__) || defined(__x86_64__))
    uint32_t low;
    uint32_t high;
    __asm__ __volatile__ ("rdtsc" : "=a" (low), "=d" (high));
    uint64_t tsc = ((uint64_t)high << 32) | low;
    #define HAS_RDTSC
  #elif (defined _MSC_VER && (defined _M_IX86 || defined _M_X64))
    uint64_t tsc = __rdtsc();
    #define HAS_RDTSC
  #elif (defined(__SUNPRO_CC) && (__SUNPRO_CC >= 0x5100) && \
        ( defined(__i386) || defined(__amd64) || defined(__x86_64) ))
    union { uint64_t u64val; uint32_t u32val[2]; } tscu;
    asm("rdtsc" : "=a" (tsc.u32val [0]), "=d" (tsc.u32val [1]));
    uint64_t tsc = tsc.u64val;
    #define HAS_RDTSC
  #endif
#endif /* USE_RDTSC_CACHE */

#if defined(HAS_RDTSC)
  // RDTSC is unreliable as a time measurement device itself, but here
  // we use it merely to optimize number of _nanotime samples.

  constexpr uint64_t CLOCK_PRECISION = 1000; // 1ms

  static int64_t last_tsc = -1;
  static int64_t last_time = -1;

  static sync_once_flag once;
  sync_once(&once, {
    last_tsc = tsc;
    last_time = _nanotime();
  });

  if (tsc - last_tsc <= (CLOCK_PRECISION / 2) && tsc >= last_tsc) {
    return last_time + ((tsc - g_clockbuf.last_tsc) / (1000000 / CLOCK_PRECISION));
  }

  if (tsc >= last_tsc && tsc - last_tsc <= (CLOCK_PRECISION / 2)) {
    // less than 1/2 ms since we sampled _nanotime
    return last_time + ((tsc - last_tsc) / (1000000 / CLOCK_PRECISION));
  }

  last_tsc = tsc;
  last_time = _nanotime();
  return last_time;
#else
  return _nanotime();
#endif
}
