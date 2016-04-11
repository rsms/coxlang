#include "atomic.h"

int _sync_once(sync_once_flag* f) {
  if (*f != 2) {
    // flag has not been raised yet
    if (__sync_bool_compare_and_swap(f, 0, 1)) {
      // We won the race
      *f = 2;
      return 1;
      // caller must now exec init code and issue full mem barrier
    }
    // Another thread won the race and we spin to await raising the flag
    while (*f != 2) { __sync_synchronize(); };
  }
  return 0;
}
