#include "event.h"
#include "log.h"
#include "common.h"
#include "task.h"
#include "sched.h"

#include <limits.h>
#include <sys/resource.h>
#include <sysexits.h>
#include <assert.h>
#include <errno.h>

#define LISTEND 0xffffffff

#define FDW_IN  1
#define FDW_OUT 2
#define FDW_ERR 4

static size_t gEvsSize = 0; // size of _evs

void Events::init() {
  // if (gEvsSize == 0) {
  //   struct rlimit rlim;
  //   if (getrlimit(RLIMIT_NOFILE, &rlim) != 0) {
  //     // TODO: throw exception or somehow pass error to user code
  //     fatal("getrlimit");
  //   }
  //   if (rlim.rlim_max == RLIM_INFINITY || rlim.rlim_max == RLIM_SAVED_MAX) {
  //     gEvsSize = OPEN_MAX;
  //   } else {
  //     gEvsSize = rlim.rlim_max;
  //   }
  //   rxlog("Events::init: gEvsSize=" << gEvsSize
  //     << " (" << (gEvsSize * sizeof(struct kevent)) << " B)");
  // }
  // _evs = (struct kevent*)malloc(gEvsSize * sizeof(struct kevent));
  // _conds = (Cond*)malloc(gEvsSize * sizeof(Cond));
  // _nevs = 0;
  _fd = kqueue();
  if (_fd == -1) {
    panic(strerror(errno));
  }
}


Events::~Events() {
  if (_evs != nullptr) {
    free(_evs);
    _evs = nullptr;
  }
  close(_fd);
}


size_t Events::pushev(uintptr_t ident, int16_t filter, uint16_t flags, Task* T) {
  // Make sure we have room for another event
  if (_nevs == _evsz) {
    _evsz = _evsz ? _evsz * 2 : 64; // 64, 128, 256, ...
    _evs = (struct kevent*)realloc((void*)_evs, _evsz);
  }
  auto& e = _evs[_nevs];

  e.ident  = ident;
  e.filter = filter;
  e.flags  = flags;
  e.fflags = 0; // filter-specific flags
  e.data   = 0;
  e.udata  = (void*)T;

  return _nevs++;
}


void Events::set(int fd, Cond c, Task* T) {
  Observer& o = _obs[uintptr_t(fd)];

  // TODO: Should we allow multiple tasks to observe a certain FD?
  //       Would that make sense and if so: in what scenario?

  // TODO: Allow adding EV_ONESHOT to flags. Would need to book-keep
  //       that in Observer so that we know to remove Observer when
  //       completed.

  // Idea: We could return Observer from this function and have the
  //       destructor do cleanup. That would also be a way to pass
  //       additional data back to the caller, like "avail to read."

  if (c & CondIORead) {
    pushev(fd, EVFILT_READ, EV_ADD, T);
    assert(o.readT == nullptr);
    o.readT = T;
  }

  if (c & CondIOWrite) {
    pushev(fd, EVFILT_WRITE, EV_ADD, T);
    assert(o.writeT == nullptr);
    o.writeT = T;
  }
}


void Events::remove(int fd, Cond c) {
  auto I = _obs.find(uintptr_t(fd));
  assert(I != _obs.end());
  Observer& o = I->second;

  if (c & CondIORead) {
    pushev(fd, EVFILT_READ, EV_DELETE, nullptr);
    assert(o.readT != nullptr);
    o.readT = nullptr;
  }

  if (c & CondIOWrite) {
    pushev(fd, EVFILT_WRITE, EV_DELETE, nullptr);
    assert(o.writeT != nullptr);
    o.writeT = nullptr;
  }

  if (o.readT == nullptr && o.writeT == nullptr) {
    _obs.erase(I);
  }
}


EvStatus Events::poll(uint64_t timeout_ms) {
  rxlog("event poll: timeout_ms=" << timeout_ms);

  if (_fd == -1) {
    init();
  }

  // Timeout
  // - A null timespec means "no timeout" / "wait forever".
  // - A zero-value timespec to kevent means "timeout immediately".
  struct timespec timeoutTS;
  struct timespec* timeout = nullptr;
  if (timeout_ms != 0) {
    timeoutTS.tv_sec = timeout_ms / 1000;
    timeoutTS.tv_nsec = (((long)timeout_ms) % 1000) * 1000000;
    timeout = &timeoutTS;
  }
  
  // Send changes to, and receive events from, the kernel.
  // Before calling kevent(), _evs represent changes.
  // After kevent() returns, _evs represent events (or: current met conditions).
  int n = 0; // number of output events
  int nchanges = _nevs;
  _nevs = 0; // reset change count
  while (1) {
    rxlog("events poll: call kevent");
    n = kevent(_fd, _evs, nchanges, _evs, _evsz, timeout);
    if (n == 0) {
      // timeout
      return EvStatus::Timeout;
    } else if (n == -1) {
      if (errno == EINTR) {
        // kqueue man page on EINTR:
        //   A signal was delivered before the timeout expired and before any
        //   events were placed on the kqueue for return.
        continue;
      } else {
        return EvStatus::Error;
      }
    }
    break;
  }

  rxlog("events poll: got " << n << " events");

  // List of observers which have conditions that changed
  Observer* oListHead = nullptr;
  Observer* oListTail = nullptr;

  for (int i = 0; i != n; ++i) {
    auto& e = _evs[i];
    
    assert(_obs.find(e.ident) != _obs.end());
    Observer& o = _obs[e.ident];
    o.cond = CondNone;
    if (oListHead == nullptr) {
      oListTail = oListHead = &o;
    } else {
      oListTail = oListTail->next = &o;
    }

    switch (e.filter) {
      case EVFILT_READ: {
        o.cond = Cond(o.cond | CondIORead);
        break;
      }
      case EVFILT_WRITE: {
        o.cond = Cond(o.cond | CondIOWrite);
        break;
      }
      default: {
        rxlog("event poll: unexpected kevent filter " << e.filter);
        break;
      }
    }
  }

  // Resume tasks per kevent ident
  Observer* o = oListHead;
  while (o != nullptr) {
    if (o->readT == o->writeT) {
      assert(o->readT != nullptr);
      o->readT->_S.resume(*o->readT, o->cond);
    } else {
      if (o->cond & CondIORead) {
        assert(o->readT != nullptr);
        o->readT->_S.resume(*o->readT, CondIORead);
      }
      if (o->cond & CondIOWrite) {
        assert(o->writeT != nullptr);
        o->writeT->_S.resume(*o->writeT, CondIOWrite);
      }
    }
    o = o->next;
  }

  return EvStatus::Success;
}


#if NOT_DEFINED
void Events::poll(uint64_t timeout_ms) {
  rxlog("events poll: timeout_ms=" << timeout_ms);

  #define DILL_CHNGSSIZE 128
  #define DILL_EVSSIZE 128

  struct kevent changes[DILL_CHNGSSIZE];
  size_t nchanges = 0;

  while(_changelist != LISTEND) {
    assert(nchanges < DILL_CHNGSSIZE); // TODO: limit while loop

    int fd = _changelist - 1;
    Ev& e = _evs[fd];

    if (e.inT != nullptr) {
      if(!(e.currevs & FDW_IN)) {
        EV_SET(&changes[nchanges], fd, EVFILT_READ, EV_ADD, 0, 0, 0);
        e.currevs |= FDW_IN;
        ++nchanges;
      }
    } else if(e.currevs & FDW_IN) {
      EV_SET(&changes[nchanges], fd, EVFILT_READ, EV_DELETE, 0, 0, 0);
      e.currevs &= ~FDW_IN;
      ++nchanges;
    }

    e.firing = 0;
    _changelist = e.next;
    e.next = 0;
  }

  struct timespec timeoutTS;
  struct timespec* timeout = nullptr;
  if (timeout_ms != 0) {
    timeoutTS.tv_sec = timeout_ms / 1000;
    timeoutTS.tv_nsec = (((long)timeout_ms) % 1000) * 1000000;
    timeout = &timeoutTS;
  }
  // Note: a zero-value timespec to kevent means "timeout immeiately".

  struct kevent evs[DILL_EVSSIZE];
  int nevs;
  do {
    rxlog("events poll: call kevent");
    nevs = kevent(_fd, changes, nchanges, evs, DILL_EVSSIZE, timeout);
    if (nevs == 0) {
      // timeout
      return;
    }
  } while (nevs < 0 && errno == EINTR);

  rxlog("events poll: kevent returned " << nevs << " events");

  // Update "firing" state of _evs from fd info in evs
  for (int i = 0; i != nevs; ++i) {
    assert(evs[i].flags != EV_ERROR); // TODO: handle error
    switch (evs[i].filter) {
      case EVFILT_READ:
      case EVFILT_WRITE: {
        int fd = (int)evs[i].ident;
        Ev& e = _evs[fd];
        if (evs[i].flags == EV_EOF) {
          e.firing |= IOEnd;
        } else {
          if (evs[i].filter == EVFILT_READ) {
            e.firing |= IORead;
          }
          if (evs[i].filter == EVFILT_WRITE) {
            e.firing |= FDW_OUT;
          }
          }
        }
      }
      
      if (evs[i].flags == EV_EOF) {
        e.firing |= FDW_ERR;
      } else {
        if (evs[i].filter == EVFILT_READ) {
          e.firing |= FDW_IN;
        }
        if (evs[i].filter == EVFILT_WRITE) {
          e.firing |= FDW_OUT;
        }
      }
      if (e.next == 0) {
        e.next = _changelist;
        _changelist = fd + 1;
      }
    }
  }

  // Resume tasks
  uint32_t chl = _changelist;
  while (chl != LISTEND) {
    int fd = chl - 1;
    Ev& e = _evs[fd];
    if (e.inT == e.outT) {
      assert(e.inT != nullptr);
      rxlog("event poll: resume inT/outT");
      e.inT->_S.resume(*e.inT, EvResultRead | EvResultWrite);
      e.inT = nullptr;
      e.outT = nullptr;
    } else {
      if (e.inT) {
        rxlog("event poll: resume inT");
        // size_t bytes_available = kevent.data;
        e.inT->_S.resume(*e.inT, EvResultRead);
        e.inT = nullptr;
      }
      if (e.outT) {
        rxlog("event poll: resume outT");
        e.inT->_S.resume(*e.inT, EvResultWrite);
        e.outT = nullptr;
      }
    }
    e.firing = 0;
    chl = e.next;
  }

  // Return 0 in case of time out. 1 if at least one coroutine was resumed.
  //return nevs > 0 ? 1 : 0;
}
#endif
