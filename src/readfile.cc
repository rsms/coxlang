#include "readfile.h"
#include <errno.h>
#include <stdlib.h>

char* readfile(FILE* f, size_t& outLen, size_t maxSize) {
  const size_t blocksize = 4096;
  char*  sp  = (char*)malloc(blocksize);
  char*  p   = sp;
  size_t cap = blocksize;

  outLen = 0;

  while (1) {
    size_t n = fread(p, 1, cap, f);

    p += n;

    if (n < cap && ferror(f)) {
      int en = errno;
      free(sp);
      errno = en;
      return nullptr;
    }

    if (feof(f)) {
      break;
    }

    cap -= n;

    if (cap < 64) {
      if (cap == maxSize) {
        free(sp);
        errno = EFBIG;
        return nullptr;
      }
      cap += blocksize;
      if (cap > maxSize) {
        cap = maxSize;
      }
      size_t offs = uintptr_t(p) - uintptr_t(sp);
      sp = (char*)realloc((void*)sp, cap);
      p = sp + offs;
    }
  }

  outLen = uintptr_t(p) - uintptr_t(sp);
  return sp;
}
