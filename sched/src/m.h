// M – machine (worker thread)
#pragma once
#include "atomic.h"
#include "t.h" // for M::t0, might be able to work around this
#include "os.h"
#include "note.h"

struct P;
using MCallFun = void(*)(M&,T&);

struct M {
  T          t0;               // task with scheduling stack
  T*         curt = nullptr;   // current running task
  uint32_t   locks = 0;        // number of locks held to this M
  T*         lockedt = nullptr;// task locked to this M
  P*         p = nullptr;      // attached p for executing Ts (null if not executing)
  P*         nextp = nullptr;
  T*         deadq = nullptr;  // dead tasks waiting to be reclaimed (TDead)
  bool       spinning = false; // m is out of work and is actively looking for work
  bool       blocked = false;  // m is blocked on a note
  TUnlockFun waitunlockf = nullptr;
  intptr_t   waitunlockv = 0;
  M*         schedlink = nullptr;
  uint32_t   fastrand = 0;     // next value for m_fastrand()
  Note       parknote;

  // Platform-specific fields
  Sema       waitsema;

  M();
};

// Switch to ct.m.t0's stack and call fn(m,t)
void m_call(T& ct, MCallFun fn);

// Returns M for the current T, with +1 refcount
M& m_acquire();

// Release m previosuly m_acquire()'d
void m_release(M&);

// Schedules gp to run on the current M.
// If inheritTime is true, gp inherits the remaining time in the
// current time slice. Otherwise, it starts a new time slice.
// Never returns.
void m_execute(M&, T&, bool inheritTime);

// One round of scheduler: find a runnable goroutine and execute it.
// Never returns.
void m_schedule(M&);

T* m_findrunnable(bool& inheritTime);

// Return a pseudo-random integer. Only for sceduling randomization etc.
uint32_t m_fastrand(M&);

// Stops execution of M until new work is available.
// Returns with acquired P. M must be M==t_get().m
void m_stop(M&);

// —————————————————————————————

inline __attribute__((always_inline)) M& m_acquire() {
  T& t = t_get();
  ++t.m->locks;
  return *t.m;
}

inline void m_release(M& m) {
  --m.locks;
}
