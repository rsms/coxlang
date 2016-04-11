#include "stack.h"
#include "atomic.h"

#include <assert.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>

// Define to enable allocating an extra page which is then marked as
// protected, causing a crash if the stack would grown into it.
// Helps find tasks which grow beyond their fixed-size stacks.
#define STACK_MPROTECT

#if !defined(SIGSTKSZ)
  #define SIGSTKSZ (8 * 1024)
#endif


// Allocates stack memory that is at least size large.
// Actual size is stored in size on return.
void* stack_alloc(size_t reqsize, size_t& outsize) {
  static size_t pagesize;
  static size_t stacksizeLimit;

  // Lazily query system for page size and stack-size limit
  static sync_once_flag once1;
  sync_once(&once1, {
    pagesize = sysconf(_SC_PAGESIZE);  // _SC_PAGESIZE def unistd.h
    rlimit limit;
    #if defined(NDEBUG)
      getrlimit(RLIMIT_STACK, &limit);
    #else
      assert(getrlimit(RLIMIT_STACK, &limit) == 0);
    #endif
    stacksizeLimit = limit.rlim_max;
  });

  if (reqsize == 0) {
    reqsize = SIGSTKSZ;
    // The value SIGSTKSZ is a system default specifying the number of bytes
    // that would be used to cover the usual case when manually allocating
    // an alternate stack area.
  }

  // Adjust size to align with pagesize
  size_t size = size_t((reqsize + pagesize - 1) & ~(pagesize - 1))
    #if defined(STACK_MPROTECT)
      + pagesize  /* extra page that we will mprotect */
    #endif
      ;

  // obey 
  if (size > stacksizeLimit) {
    size = stacksizeLimit;
  }

  // allocate memory pages
  // TODO: investigate if posix_memalign would be more efficient
#if defined(MAP_ANON)
  void* p = mmap(0, size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
#else
  void* p = mmap(0, size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
#endif
  if (p == MAP_FAILED) {
    return nullptr;
  }

  // Protect top of stack
  #if defined(STACK_MPROTECT)
  if (mprotect(p, pagesize, PROT_NONE) != 0) {
    int e = errno;
    munmap(p, size); // could fail, but we are already failing
    errno = e;
    return nullptr;
  }
  #endif

  outsize = size;

  // We return the SB, meaning the returned value points to the
  // bottom of the stack (end of memory segment.) C stack grows upwards.
  return ((char*)p) + size;
}


void stack_dealloc(void* sb, size_t size) {
  void* p = ((char*)sb) - size;

  // Unprotect top memory page so we can free it.
  // Seems not to be needed when using mmap.
  // #if defined(STACK_MPROTECT)
  // #if NDEBUG
  // assert(mprotect(p, pagesize, PROT_READ|PROT_WRITE) == 0);
  // #else
  // assert(mprotect(p, pagesize, PROT_READ|PROT_WRITE) == 0);
  // #endif
  // #endif

  #if NDEBUG
  munmap(p, size);
  #else
  assert(munmap(p, size) == 0);
  #endif
}
