#pragma once
#include <stdint.h>
#include <stdio.h>

// Read up to maxSize bytes from f. Returns nullptr and sets errno on error.
// Caller is responsible for free()ing the returned pointer.
char* readfile(FILE* f, size_t& outLen, size_t maxSize);
