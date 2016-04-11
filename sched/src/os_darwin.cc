#include "os.h"
#include "common.h"
#include <errno.h>
#include <mach/mach_init.h>
#include <mach/mach_error.h>
#include <mach/task.h>

// struct mach_timespec {
//     unsigned int tv_sec;
//     clock_res_t tv_nsec;
// };
// extern  kern_return_t semaphore_signal      (semaphore_t semaphore);
// extern  kern_return_t semaphore_signal_all  (semaphore_t semaphore);
// extern  kern_return_t semaphore_wait        (semaphore_t semaphore);
// extern  kern_return_t semaphore_timedwait     (semaphore_t semaphore, 
//                          mach_timespec_t wait_time);
// extern  kern_return_t semaphore_timedwait_signal(semaphore_t wait_semaphore,
//                            semaphore_t signal_semaphore,
//                            mach_timespec_t wait_time);
// extern  kern_return_t   semaphore_wait_signal   (semaphore_t wait_semaphore,
//                                                  semaphore_t signal_semaphore);
// extern  kern_return_t semaphore_signal_thread (semaphore_t semaphore,
//                                                  thread_t thread);

bool sema_create(Sema& s, int initval) {
  if (s.v != SEMAPHORE_NULL) {
    return false;
  }
  mach_port_t mtask = mach_task_self();
  kern_return_t r = semaphore_create(mtask, &s.v, SYNC_POLICY_FIFO, initval);
  if (r != KERN_SUCCESS) {
    panic(mach_error_string(r));
  }
  return true;
}

// If ns < 0, try to acquire s (without any deadline) and return 0.
// If ns >= 0, try to acquire s for at most ns nanoseconds.
//    Return 0 if the semaphore was acquired, -1 if interrupted or timed out.
int sema_sleep(Sema& s, uint64_t ns) {
  if (ns >= 0) {
    // try to acquire s for at most ns nanoseconds
    mach_timespec_t ts;
    if (ns > 1000000000) {
      ts.tv_sec = (decltype(ts.tv_sec))(ns / 1000000000);
      ts.tv_nsec = (decltype(ts.tv_nsec))((ns % 1000000000) * 1000000000);
    } else {
      ts.tv_sec = 0;
      ts.tv_nsec = ns;
    }
    kern_return_t r = semaphore_timedwait(s.v, ts);
    if (r == KERN_ABORTED || r == KERN_OPERATION_TIMED_OUT) {
      return -1;
    }
    if (r != 0) {
      panic(mach_error_string(r));
    }
    return 0;
  }

  // acquire s and return 0
  kern_return_t r;
  while ((r = semaphore_wait(s.v)) != 0) {
    if (r != KERN_ABORTED) {
      panic(mach_error_string(r));
    }
    // interrupted; try again
  }
  return 0;
}

// Wake up s, which is or will soon be sleeping
void sema_wake(Sema& s) {
  kern_return_t r;
  while ((r = semaphore_signal(s.v)) != 0) {
    if (r != KERN_ABORTED) {
      panic(mach_error_string(r));
    }
    // interrupted; try again
  }
}
