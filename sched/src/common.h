// commonly used internal definitions
#pragma once
#include "cox.h"

#include <cstddef>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// The classic stringifier macro
#define STR1(str) #str
#define STR(str)  STR1(str)

// HI_FILENAME expands to the current translation unit's filename
#ifdef __BASE_FILE__
  #define COX_FILENAME __BASE_FILE__
#else
  #define COX_FILENAME __FILE__
#endif

#define fwd(x) std::forward<decltype(x)>(x)

#if __has_attribute(always_inline)
  #define ALWAYS_INLINE __attribute__((always_inline)) inline
#else
  #define ALWAYS_INLINE inline
#endif

ALWAYS_INLINE void panic(const char* descr) {
  fputs(descr, stderr);
  abort();
}

#define TODO_IMPL do { \
  fprintf(stderr, "TODO implement %s  %s:%d\n", \
    __PRETTY_FUNCTION__, __FILE__, __LINE__); \
  abort(); \
} while (0)

#define TODO_SECTION do { \
  fprintf(stderr, "TODO implement section in %s  %s:%d\n", \
    __PRETTY_FUNCTION__, __FILE__, __LINE__); \
  abort(); \
} while (0)
