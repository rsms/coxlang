#pragma once
// TODO: #if-conditionaed includes

#include "status.h"
#include "cond.h"
#include <sys/event.h>
#include <map>

struct Task;

// Returned from suspend()
enum EvResult : intptr_t {
  EvResultErr = 0,
  EvResultRead = 1,
  EvResultWrite = 2,
};

// Returned from poll()
enum class EvStatus {
  // Status returned from poll()
  Closed = 0,   // The event queue/system is closed.
  Success,      // Did some work
  Interrupted,  // Interrupted by a different thread.
  Timeout,      // Timeout
  Error,        // An error occured. Check errno for details.
};

// enum class EvTrigger { Once, Continuously };

struct Events {
  ~Events();

  void set(int fd, Cond, Task* T);
  void remove(int fd, Cond);

  EvStatus poll(uint64_t timeout_ms);

private:
  void init();
  size_t pushev(uintptr_t ident, int16_t filter, uint16_t flags, Task*);

  int _fd = -1; // kqueue fd

  // kevents buffer used for both input and output from kevent().
  // This is possible due to the restriction that _kevs is only modified
  // in the same thread as that calling poll().
  struct kevent* _evs = nullptr;
  size_t         _evsz = 0; // size of _evs
  size_t         _nevs = 0; // number of used kevents in _evs

  // Observer represents everything observing a certain fd
  struct Observer {
    // Per-filter waiting task
    Task* readT  = nullptr; // for CondIORead
    Task* writeT = nullptr; // for CondIOWrite

    // Used only during event triggering:
    Cond      cond = CondNone; // conditions met
    Observer* next = nullptr; // used for lists
  };
  // kevent.ident => Observer
  std::map<uintptr_t,Observer> _obs;
};
