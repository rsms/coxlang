#pragma once
#include <stdlib.h>
#include <assert.h>

struct SrcLoc {
  size_t   offset = 0; // byte offset into source
  uint32_t length = 0; // number of source bytes
  uint32_t line   = 0; // zero-based
  uint32_t column = 0; // zero-based, expressed in source bytes (not Unicode chars)

  void extend(const SrcLoc& later) {
    assert((later.offset + later.length) > offset);
    length = (later.offset + later.length) - offset;
  }
};
