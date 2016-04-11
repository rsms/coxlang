#include "strtou64.h"
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

bool strtou64(const char* pch, size_t size, int base, uint64_t& result) {
  const char* s = pch;
  const char* end = pch + size;
  uint64_t acc = 0;
  uint64_t cutoff;
  int neg, any, cutlim;
  char c;

  if (base < 2 || base > 36) {
    return false; // invalid base
  }

  any = 0;
  cutoff = ULLONG_MAX;
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
  result = (int64_t)acc;
  return true;
}
