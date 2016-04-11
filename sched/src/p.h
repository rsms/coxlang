// P - processor; a resource that is required to execute tasks.
//     M must have an associated P to execute T,
//     however it can be blocked or in a syscall w/o an associated P.
#pragma once
#include "atomic.h"

enum PStatus : uint32_t {
  // P status
  PIdle      = 0,
  PRunning, // 1 Only this P is allowed to change from _Prunning.
  PSyscall, // 2
  PDead,    // 3
};

struct T;
struct M;

struct P {
  uint32_t schedtick = 0; // incremented on every scheduler call
  uint32_t ident = 0;     // corresponds to offset in gs.allp
  PStatus  status;
  M*       m = nullptr;    // back-link to associated m (nil if idle)
  P*       link = nullptr;

  // Queue of runnable tasks. Accessed without lock.
  static constexpr uint32_t runqsize = 256; // should be power-of two
  atomic_t(uint32_t) runqhead = 0;
  atomic_t(uint32_t) runqtail = 0;
  T*                 runq[runqsize];
  // runnext, if non-nil, is a runnable T that was ready'd by
  // the current T and should be run next instead of what's in
  // runq if there's time remaining in the running T's time
  // slice. It will inherit the time left in the current time
  // slice. If a set of goroutines is locked in a
  // communicate-and-wait pattern, this schedules that set as a
  // unit and eliminates the (potentially large) scheduling
  // latency that otherwise arises from adding the ready'd
  // goroutines to the end of the run queue.
  atomic_t(T*)       runnext;

  // Ts – local cache of dead T's.
  T*         tfree = nullptr;
  uint32_t   tfreecount = 0;

};

// One round of scheduler: find a runnable task and execute it.
// Never returns.
void p_schedule(P&);

// Tries to put T on the local runnable queue.
// If next if false, runqput adds g to the tail of the runnable queue.
// If next is true, runqput puts g in the p.runnext slot.
// If the run queue is full, runnext puts g on the global queue.
// Executed only by the owner P.
void p_runqput(P&, T&, bool next);

// Get T from local runnable queue.
// If inheritTime is true, T should inherit the remaining time in the
// current time slice. Otherwise, it should start a new time slice.
// Executed only by the owner P.
T* p_runqget(P&, bool& /*out*/ inheritTime);

// True if p has no Ts on its local run queue.
// Note that this test is generally racy.
inline bool p_runqisempty(P& p) {
  return p.runqhead == p.runqtail && p.runnext == 0;
}

// Steal half of elements from local runnable queue of p2
// and put onto local runnable queue of p.
// Returns one of the stolen elements (or null if failed).
T* p_runqsteal(P& p, P& p2, bool stealRunNextG);


// Reclaim dead T, to be reused for new tasks. Puts on tfree list.
// If local list is too long, transfer a batch to the global list.
void p_tfreeput(P&, T&);

// Get from tfree list. Returns nullptr if no free T was available.
T* p_tfreeget(P&);

// Purge all cached T's from P.tfree list to the global list.
void p_tfreepurge(P&);


// Associate P and the current M.
// Essentially: m=t_get().m; m.p = p; p.m = m; p.status=PRunning.
void p_acquire(P&);

// Disassociate current P and M. P must be P==t_get().m.p.
// Essentially: p.m.p=null; p.m=null; p.status=PIdle.
void p_release(P&);

// Hands off P from syscall or locked M.
void p_handoff(P&);


// Schedules some M to run the p (creates an M if necessary).
// If p==nullptr, tries to get an idle P, if no idle P's does nothing.
// May run with m.p==null.
// If spinning is set, the caller has incremented nmspinning and startm will
// either decrement nmspinning or set m.spinning in the newly started M.
void p_startm(P* p, bool spinning);
