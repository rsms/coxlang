#include <mach/semaphore.h>

struct Sema { semaphore_t v = SEMAPHORE_NULL; };
  // Note: semaphore_t is a 32-bit integer on Darwin 15.4.0 and back.
