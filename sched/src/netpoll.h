#pragma once
#include <sys/event.h>
#include <mutex>
#include "freelist.h"

struct T;
struct Timer {}; // TODO

// Network poller descriptor
struct PollDesc {
  FreeListEntryFields(PollDesc) // in pollCache free-list

  // The lock protects pollOpen, pollSetDeadline, pollUnblock and deadlineimpl
  // operations. This fully covers seq, rt and wt variables. fd is constant
  // throughout the PollDesc lifetime. pollReset, pollWait, pollWaitCanceled
  // and runtime·netpollready (IO readiness notification) proceed w/o taking
  // the lock. So closing, rg, rd, wg and wd are manipulated in a lock-free way
  // by all operations.

  std::mutex lock;     // protects the following fields

  intptr_t   fd;
  bool       closing;
  uintptr_t  seq;      // protects from stale timers and ready notifications
  
  T*         rt;     // pdReady, pdWait, T waiting for read or nil
  intptr_t   rz;     // number of bytes available to read, or -1 if unknown
  Timer      rtimer; // read deadline timer (set if rt.f != nil)
  int64_t    rd;     // read deadline
  
  T*         wt;     // pdReady, pdWait, T waiting for write or nil
  intptr_t   wz;     // number of bytes available in write buffer, or -1 if unknown
  Timer      wtimer; // write deadline timer
  int64_t    wd;     // write deadline

  uint32_t   user; // user settable cookie

  // evicts fd from the pending list, unblocking any I/O running on this PollDesc.
  void evict();
};

struct PollCache {
  PollDesc& alloc();
  void free(PollDesc&);
private:
  FreeList<PollDesc> _freelist;
};

// True if netpoll is active. Optimization used by scheduler.
bool netpoll_active();

// Initialize the process-wide netpoll system. Thread-safe.
// First call initializes the system, subsequent calls do nothing.
void netpoll_init();

// Start observing events to fd. Events are edge-triggered, meaning that
// when e.g. fd becomes readable, only one event will be issued (until
// there's more data to be read or EOF happens.)
// The caller should register tasks on the returned PollDesc for reading
// and/or writing.
// Returns nullptr and sets errno on failure.
PollDesc* netpoll_open(intptr_t fd);

// Close pd. After this call, pd is invalid.
void netpoll_close(PollDesc& pd);

enum PollStrategy {
  PollImmediate = 0, // return already-ready tasks immediately, or null.
  PollBlocking,      // wait until there's at least one ready task.
};

// returns true if IO is ready, or false if timedout or closed.
// • mode     - must be either 'r' or 'w' (can't be both)
// • strategy - when Blocking: wait only for completed IO and ignore errors.
bool netpoll_await(PollDesc& pd, int mode, PollStrategy);

// Polls for ready network connections.
// Returns list of tasks that become runnable.
T* netpoll_poll(PollStrategy);
