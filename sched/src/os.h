// System-specific types and functions
#pragma once
#if _DEFINITION_STUBS_

typedef x Sema; // semaphore type

#endif
#include "target.h"
//include "os_{RX_TARGET_OS_KIND}.h"
#define __OS_HEADER(x) #x
#define _OS_HEADER(x) __OS_HEADER(os_##x.h)
#define OS_HEADER(x) _OS_HEADER(x)
#include OS_HEADER(RX_TARGET_OS_KIND)

// #if   defined(RX_TARGET_OS_DARWIN)
// #include "os-darwin.h"
// #elif defined(RX_TARGET_OS_LINUX)
// #include "os-linux.h"
// #elif defined(RX_TARGET_OS_WINDOWS)
// #include "os-windows.h"
// #else
// #error "Unsupported system"
// #endif /* defined(RX_TARGET_OS_...) */

// Create semaphore with inital value initval.
// If s is already initialized, this function returns immediately with false.
// Otherwise, when s was initialized, true is returned.
bool sema_create(Sema& s, int initval);

// If ns < 0, acquire s and return 0.
// If ns >= 0, try to acquire s for at most ns nanoseconds.
// Return 0 if the semaphore was acquired, -1 if interrupted or timed out.
int sema_sleep(Sema& s, uint64_t ns);

// Wake up s, which is or will soon be sleeping
void sema_wake(Sema& s);

// Close-on-exec; the given file descriptor will be automatically closed
// in the successor process after fork or execv.
// Returns 0 on success, -1 on error (check errno.)
int closeonexec(int fd);

// Configure fd to use non-blocking I/O.
int nonblock(int fd);
