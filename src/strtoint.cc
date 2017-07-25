#include "strtoint.h"
// #include <sys/cdefs.h>
#include <limits.h>
// #include <errno.h>
// #include <ctype.h>
// #include <stdlib.h>

// static inline int isupper(char c) {
//   return (c >= 'A' && c <= 'Z');
// }

// static inline int isalpha(char c) {
//   return ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'));
// }

// static inline int isspace(char c) {
//   return (c == ' ' || c == '\t' || c == '\n' || c == '\12');
// }

// static inline int isdigit(char c) {
//   return (c >= '0' && c <= '9');
// }

template <typename UInt, UInt UIntMax>
static inline bool strtou(const char* pch, size_t size, int base, UInt& result) {
  const char* s = pch;
  const char* end = pch + size;
  UInt acc = 0;
  UInt cutoff;
  int neg, any, cutlim;
  char c;

  if (base < 2 || base > 36) {
    return false; // invalid base
  }

  any = 0;
  cutoff = UIntMax;//ULLONG_MAX;
  cutlim = cutoff % base;
  cutoff /= base;
  for (c = *s ; s != end; c = *s++) {
    if (c >= '0' && c <= '9') {
      c -= '0';
    } else if (c >= 'A' && c <= 'Z') {
      c -= 'A' - 10;
    } else if (c >= 'a' && c <= 'z') {
      c -= 'a' - 10;
    } else {
      return false;
    }
    if (c >= base) {
      return false;
    }
    if (any < 0 || acc > cutoff || (acc == cutoff && c > cutlim)) {
      any = -1;
    } else {
      any = 1;
      acc *= base;
      acc += c;
    }
  }
  if (any < 0 ||  // more digits than what fits in uint64
      any == 0)
  {
    return false;
  }
  result = acc;
  return true;
}

bool strtou64(const char* p, size_t size, int base, uint64_t& result) {
  return strtou<uint64_t,0xffffffffffffffffULL>(p, size, base, result);
}

bool strtou32(const char* p, size_t size, int base, uint32_t& result) {
  return strtou<uint32_t,0xffffffffU>(p, size, base, result);
}
