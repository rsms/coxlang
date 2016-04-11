#include "gs.h"
#include "t.h"
#include "m.h"
#include "p.h"
#include "time.h"
#include "netpoll.h"
#include "log.h"
#include "common.h"

using ScopeLock = std::lock_guard<std::mutex>;

GS gs;
M* m0;   // main thread worker


// Try get a batch of G's from the global runnable queue.
// gs must be locked. (go:globrunqget)
T* gs_runqget(P& p, uint32_t max) {
  if (gs.runqsize == 0) {
    return nullptr;
  }

  // determine amount of Ts to get
  auto n = gs.runqsize / gs.maxprocs + 1;
  if (n > gs.runqsize) {
    n = gs.runqsize;
  }
  if (max > 0 && n > max) {
    n = max;
  }
  if (n > p.runqsize / 2) { // ok, runqsize is 2^n
    n = p.runqsize / 2;
  }

  gs.runqsize -= n;
  if (gs.runqsize == 0) {
    // we are getting everything
    gs.runqtail = (T*)nullptr;
  }

  // Take top T, to be returned
  T* tp = gs.runqhead;
  gs.runqhead = tp->schedlink;

  // Move n Ts from top of gs.runq to end of p.runq
  while (--n > 0) {
    T* tp1 = gs.runqhead;
    gs.runqhead = tp1->schedlink;
    p_runqput(p, *tp1, /*next=*/false);
  }

  return tp;
}

// Put t in the global runnable queue tail. gs must be locked.
void gs_runqput(T& t) {
  t.schedlink = nullptr;
  if (gs.runqtail != nullptr) {
    gs.runqtail->schedlink = &t;
  } else {
    gs.runqhead = &t;
  }
  gs.runqtail = &t;
  ++gs.runqsize;
}

// Put t in global runnable queue head. gs must be locked.
void gs_runqputhead(T& t) {
  t.schedlink = gs.runqhead;
  gs.runqhead = &t;
  if (gs.runqtail == nullptr) {
    gs.runqtail = &t;
  }
  ++gs.runqsize;
}


// Injects the list of runnable T's into the scheduler
void gs_runqinject(T* tlist) {
  if (tlist == nullptr) {
    return;
  }
  int n = 0;
  { ScopeLock gslock(gs.lock);
    for (; tlist != nullptr; ++n) {
      T* tp = tlist;
      tlist = tp->schedlink;
      t_casstatus(*tp, TWaiting, TRunnable);
      gs_runqput(*tp);
    }
  }
  // TODO:
  // for (; n != 0; --n) {
  //   startm(nullptr, false);
  // }
}

// Try get a P from gs.pidle list. gs must be locked.
P* gs_pidleget() {
  P* pp = gs.pidle;
  if (pp != nullptr) {
    gs.pidle = pp->link;
    AtomicXAdd(&gs.pidlecount, -1);
  }
  return pp;
}

// Put P to on gs.pidle list. gs must be locked.
void gs_pidleput(P& p) {
  assert(p_runqisempty(p) /* trying to put P to sleep with runnable Ts */);
  p.link = gs.pidle;
  gs.pidle = &p;
  AtomicXAdd(&gs.pidlecount, 1);
}

// Check for deadlock situation.
// The check is based on number of running M's, if 0 -> deadlock.
static void gs_checkdeadlock() { // func checkdead()
  TODO_IMPL;
}

// Put mp on midle list. gs must be locked.
void gs_midleput(M& m) {
  m.schedlink = gs.midle;
  gs.midle = &m;
  ++gs.midlecount;
  gs_checkdeadlock();
}

// Try to get an m from midle list. gs must be locked.
M* gs_midleget() {
  M* mp = gs.midle;
  if (mp != nullptr) {
    gs.midle = mp->schedlink;
    --gs.midlecount;
  }
  return mp;
}


// Finds a runnable goroutine to execute.
// Tries to steal from other P's, get g from global queue, poll network.
T* gs_findrunnable(bool& inheritTime) {
  T& ct = t_get();
  assert(ct.m != nullptr);
  M& m = *ct.m;
  assert(m.p != nullptr);
  P& p = *m.p;

top:
  // local runq
  T* tp = p_runqget(p, inheritTime);
  if (tp != nullptr) {
    return tp;
  }

  inheritTime = false;

  // global runq
  if (gs.runqsize != 0) {
    ScopeLock gslock(gs.lock);
    tp = gs_runqget(p, 0);
    if (tp != nullptr) {
      return tp;
    }
  }

  // Poll network.
  // This netpoll is only an optimization before we resort to stealing.
  // We can safely skip it if there a thread blocked in netpoll already.
  // If there is any kind of logical race with that blocked thread
  // (e.g. it has already returned from netpoll, but does not set lastpoll yet),
  // this thread will do blocking netpoll below anyway.
  if (netpoll_active() && gs.lastpoll != 0) {
    rxlog("gs_findrunnable: netpoll_poll(PollImmediate)");
    tp = netpoll_poll(PollImmediate); // non-blocking
    // netpoll returns list of tasks linked by schedlink.
    if (tp != nullptr) {
      // Put all but the top task into runq
      gs_runqinject(tp->schedlink);
      // Return the first T returned from netpoll
      t_casstatus(*tp, TWaiting, TRunnable);
      return tp;
    }
  }

  // If number of spinning M's >= number of busy P's, block.
  // This is necessary to prevent excessive CPU consumption
  // when gs.maxprocs>>1 but the program parallelism is low.
  if (!m.spinning &&
      AtomicLoad(&gs.nmspinning) * 2
      >= int32_t(gs.maxprocs - AtomicLoad(&gs.pidlecount)) )
  {
    goto stop;
  }
  if (!m.spinning) {
    m.spinning = true;
    AtomicXAdd(&gs.nmspinning, 1);
  }

  // random steal from other P's
  for (uint32_t i = 0, n = gs.maxprocs * 4; i < n; ++i) {
    P& pvictim = *gs.allp[m_fastrand(m) % gs.maxprocs];
    T* tp = nullptr;
    if (&pvictim == &p) {
      // current P
      bool _;
      tp = p_runqget(p, _);
    } else {
      // first look for ready queues with more than one T
      bool stealRunNextG = i > 2 * gs.maxprocs;
      tp = p_runqsteal(p, pvictim, stealRunNextG);
    }
    if (tp != nullptr) {
      return tp;
    }
  }

stop:

  // We have nothing to do. We could run idle work here,
  // like cleaning up, pruning excessive freelists, etc.

  // return P and block
  { ScopeLock lock(gs.lock);
    if (gs.runqsize != 0) {
      // A task was added to the global runq while we didn't have gs.lock
      return gs_runqget(p, 0);
    }
    p_release(p);
    gs_pidleput(p);
  }

  // Delicate dance: thread transitions from spinning to non-spinning state,
  // potentially concurrently with submission of new tasks. We must
  // drop nmspinning first and then check all per-P queues again (with
  // StoreLoad memory barrier in between). If we do it the other way around,
  // another thread can submit a goroutine after we've checked all run queues
  // but before we drop nmspinning; as the result nobody will unpark a thread
  // to run the goroutine.
  // If we discover new work below, we need to restore m.spinning as a signal
  // for resetspinning to unpark a new worker thread (because there can be more
  // than one starving goroutine). However, if after discovering new work
  // we also observe no idle Ps, it is OK to just park the current thread:
  // the system is fully loaded so no spinning threads are required.
  // Also see "Worker thread parking/unparking" in doc/sched.md.
  bool wasSpinning = m.spinning;
  if (m.spinning) {
    m.spinning = false;
    if (AtomicXAdd(&gs.nmspinning, -1) < 0) {
      panic("negative nmspinning");
    }
  }

  // check all runqueues once again
  for (uint32_t i = 0; i < gs.maxprocs; ++i) {
    P* pp = gs.allp[i];
    if (pp != nullptr && !p_runqisempty(*pp)) {
      // pp has runnable tasks; get an idle P instead:
      { ScopeLock lock(gs.lock);
        pp = gs_pidleget();
      }
      if (pp != nullptr) {
        // found an idle P: acquire it (associate with m)
        p_acquire(*pp);
        if (wasSpinning) {
          m.spinning = true;
          AtomicXAdd(&gs.nmspinning, 1);
        }
        // retry gs_findrunnable with newly started P
        goto top;
      }
      // didn't find any idle P
      break;
    }
  }

  // poll network
  if (netpoll_active() && AtomicXchg(&gs.lastpoll, 0, AtomicConsume) != 0) {
    if (m.p != nullptr) {
      panic("netpoll with p");
    }
    if (m.spinning) {
      panic("netpoll with spinning");
    }
    rxlog("gs_findrunnable: netpoll_poll(PollBlocking)");
    tp = netpoll_poll(PollBlocking); // block until new work is available
    AtomicStore(&gs.lastpoll, nanotime());
    if (tp != nullptr) {
      // Get an idle P
      P* pp;
      { ScopeLock gslock(gs.lock);
        pp = gs_pidleget();
      }
      if (pp != nullptr) {
        // We found an idle P -- put it to work
        p_acquire(*pp);
        // Move all but the first runnable to global runq
        gs_runqinject(tp->schedlink);
        // Make top of tp list runnable and return it
        t_casstatus(*tp, TWaiting, TRunnable);
        return tp;
      }

      // No idle Ps -- move tasks to global runq
      gs_runqinject(tp);
    }
  }

  m_stop(m); // Stop M and wait until someone starts it again,
  goto top;  // then try again to find a runnable T.
}


// Tries to add one more P to execute G's. Might do nothing.
// Called when a G is made runnable (newproc, ready).
void gs_wakep() {
  // be conservative about spinning threads
  if (!AtomicCasRelAcq(&gs.nmspinning, 0, 1)) {
    return;
  }
  p_startm(nullptr, true);
}

// Change number of processors. gs must be locked. The world is stopped.
// Returns list of Ps with local work; they need to be scheduled by the caller.
P* gs_procresize(uint32_t nprocs) {
  uint32_t old = gs.maxprocs;
  assert(old <= MaxMaxprocs);
  assert(nprocs > 0 && nprocs <= MaxMaxprocs);
  
  // initialize new P's
  for (uint32_t i = 0; i < nprocs; i++) {
    P* pp = gs.allp[i];
    if (pp == nullptr) {
      pp = new P;
      pp->ident = i;
      pp->status = PIdle;
      AtomicStore((volatile atomic_t(P*)*)&gs.allp[i], pp);
    }
  }

  // free unused P's
  for (uint32_t i = nprocs; i < old; i++) {
    assert(gs.allp[i] != nullptr);
    P& p = *gs.allp[i];

    // move all runnable tasks to the global queue
    while (p.runqhead != p.runqtail) {
      // pop from tail of local queue
      --p.runqtail;
      T& t = *p.runq[p.runqtail % p.runqsize];
      // push onto head of global queue
      gs_runqputhead(t);
    }
    if (p.runnext != nullptr) {
      gs_runqputhead(*p.runnext);
      p.runnext = (decltype(p.runnext))(nullptr);
    }
    // move p.tfree to gs.tfree
    p_tfreepurge(p);

    p.status = PDead;
    // can't free P itself because it can be referenced by an M in syscall
  }

  T& ct = t_get();
  assert(ct.m != nullptr);
  M& m = *ct.m;

  if (m.p != nullptr && m.p->ident < nprocs) {
    // continue to use the current P
    m.p->status = PRunning;
  } else {
    // release the current P and acquire gs.allp[0]
    if (m.p != nullptr) {
      m.p->m = nullptr;
      m.p = nullptr;
    }
    P& p = *gs.allp[0];
    p.m = nullptr;
    p.status = PIdle;
    p_acquire(p); // associate P and current M (p.m=m, m.p=p, p.status=PRunning)
  }

  P* runnablePs = nullptr;
  uint32_t i = nprocs;
  do {
    P& p = *gs.allp[--i];
    if (m.p == &p) {
      continue;
    }
    p.status = PIdle;
    if (p_runqisempty(p)) {
      gs_pidleput(p);
    } else {
      p.m = gs_midleget();
      p.link = runnablePs;
      runnablePs = &p;
    }
  } while (i != 0);

  gs.maxprocs = nprocs;

  // Sync memory with full barrier as we changed allp and maxprocs
  AtomicFence(AtomicSeqCst);

  return runnablePs;
}


// called on the program main thread
void gs_bootstrap() {
  m0 = new M;

  // allocate processor attached to current M (m0)
  m0->p = new P;
  P& p = *m0->p;
  p.status = PRunning;
  p.m = m0;

  // Set current task to root task of m0
  _tlt = &m0->t0;

  gs.maxmcount = 10000;
  gs.lastpoll = nanotime();

  // seed fastrand for m0
  m0->fastrand = uint32_t(gs.lastpoll);

  gs_procresize(1);
}


void gs_maincancel() {
  // TODO
}
