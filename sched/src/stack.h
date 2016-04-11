#pragma once
#include <stddef.h>

// Allocates stack memory that is at least reqsize large.
// If reqsize is zero, the recommended minimum stack size is allocated.
// size contains actual size in bytes.
// Returns pointer p to base of stack, i.e. memory segment is [p-size - p).
void* stack_alloc(size_t reqsize, size_t& size);

// Free stack memory at p of size.
void stack_dealloc(void* p, size_t size);
