#include "gs.h"
#include "p.h"
#include "m.h"
#include "t.h"
#include "log.h"
#include <assert.h>
#include <unistd.h> /*usleep*/

using ScopeLock = std::lock_guard<std::mutex>;

// One round of scheduler: find a runnable goroutine and execute it.
// Never returns.
void p_schedule(P&) {
  // STUB
}

// Put t and a batch of work from local runnable queue on global queue.
// Executed only by the owner P.
static bool p_runqputslow(P& p, T& t, uint32_t head, uint32_t tail) {
  // TODO
  return false;
}

// runqput tries to put t on the local runnable queue.
// If next if false, runqput adds g to the tail of the runnable queue.
// If next is true, runqput puts g in the p.runnext slot.
// If the run queue is full, runnext puts g on the global queue.
// Executed only by the owner P.
void p_runqput(P& p, T& t, bool next) {
  // if randomizeScheduler && next && fastrand1()%2 == 0 {
  //   next = false
  // }
  T* tp = &t;

  if (next) {
    // puts t in the p.runnext slot.
    T* oldnext = p.runnext;
    while (!AtomicCasRelAcq(&p.runnext, &oldnext, &t)) {
      // Note that AtomicCasRelAcq loads current value into oldnext on CAS failure.
    }

    if (oldnext == nullptr) {
      return;
    }
    // Kick the old runnext out to the regular run queue.
    // TODO: shouldn't we kick it onto the head, rather than the tail?
    tp = oldnext;
  }

  while (1) {
    auto head = AtomicLoad(&p.runqhead); // load-acquire, sync with consumers
    auto tail = p.runqtail;
    if (tail - head < p.runqsize) {
      p.runq[tail % p.runqsize] = tp;
      AtomicStore(&p.runqtail, tail + 1);
        // ^ store-release, makes the item available for consumption
      return;
    }
    // Put t and move half of the locally scheduled runnables to global runq
    if (p_runqputslow(p, *tp, head, tail)) {
      return;
    }
    // the queue is not full, now the put above must suceed. retry...
  }
}

// Get t from local runnable queue.
// If inheritTime is true, t should inherit the remaining time in the
// current time slice. Otherwise, it should start a new time slice.
// Executed only by the owner P.
T* p_runqget(P& p, bool& /*out*/ inheritTime) {
  // If there's a runnext, it's the next G to run.
  T* next = p.runnext;
  while (1) {
    if (next == nullptr) {
      break;
    }
    if (AtomicCasRelAcq(&p.runnext, &next, nullptr)) {
      inheritTime = true;
      return next;
    }
  }

  inheritTime = false;

  while (1) {
    auto head = AtomicLoad(&p.runqhead); // load-acquire, sync with consumers
    auto tail = p.runqtail;
    if (tail == head) {
      // empty
      return nullptr;
    }
    T* tp = p.runq[head % p.runqsize];
    if (AtomicCasRelAcq(&p.runqhead, &head, head + 1)) {
      // cas-release, commits consume
      return tp;
    }
  }
}

// Grabs a batch of goroutines from _p_'s runnable queue into batch.
// Batch is a ring buffer starting at batchHead.
// Returns number of grabbed goroutines.
// Can be executed by any P.
static inline uint32_t p_runqgrab(
  P& p,
  T* batch[256],
  uint32_t batchHead,
  bool stealRunNextG )
{
  while (1) {
    // load-acquire, synchronize with other consumers:
    uint32_t head = AtomicLoad(&p.runqhead);
    // load-acquire, synchronize with the producer:
    uint32_t tail = AtomicLoad(&p.runqtail);
    uint32_t n = tail - head;
    n = n - n/2;
    if (n == 0) {
      if (stealRunNextG) {
        // Try to steal from p.runnext.
        T* next = p.runnext;
        if (next != nullptr) {
          // Sleep to ensure that p isn't about to run the T we
          // are about to steal.
          // The important use case here is when the T running on p
          // ready()s another T and then almost immediately blocks.
          // Instead of stealing runnext in this window, back off
          // to give p a chance to schedule runnext. This will avoid
          // thrashing Ts between different Ps.
          usleep(100);
          if (!AtomicCasRelAcq(&p.runnext, &next, nullptr)) {
            continue;
          }
          batch[batchHead % p.runqsize] = next;
          return 1;
        }
      }
      return 0;
    }
    if (n > p.runqsize / 2) { // read inconsistent head and tail
      continue;
    }
    for (uint32_t i = 0; i < n; ++i) {
      T* tp = p.runq[(head+i) % p.runqsize];
      batch[(batchHead+i) % p.runqsize] = tp;
    }
    auto expect = head;
    if (AtomicCasWeak(&p.runqhead, &expect, head+n, AtomicRelease, AtomicConsume)) {
      // cas-release, commits consume
      return n;
    }
  }
}

// Steal half of elements from local runnable queue of p2
// and put onto local runnable queue of p.
// Returns one of the stolen elements (or null if failed).
T* p_runqsteal(P& p, P& p2, bool stealRunNextG) {
  uint32_t tail = p.runqtail;
  uint32_t n = p_runqgrab(p2, p.runq, tail, stealRunNextG);
  if (n == 0) {
    return nullptr;
  }
  --n;
  T* tp = p.runq[(tail+n) % p.runqsize];
  if (n == 0) {
    return tp;
  }

  #ifndef NDEBUG
  // load-acquire, synchronize with consumers:
  uint32_t head = AtomicLoad(&p.runqhead);
  assert(tail-head+n < p.runqsize /*else: runq overflow*/);
  #else
  AtomicLoad(&p.runqhead);
  #endif

  // store-release, makes the item available for consumption:
  AtomicStore(&p.runqtail, tail+n);
  return tp;
}

// Put on tfree list.
// If local list is too long, transfer a batch to the global list.
void p_tfreeput(P& p, T& t) {
  assert(t_readstatus(t) == TDead);

  t.schedlink = p.tfree;
  p.tfree = &t;
  ++p.tfreecount;

  // TODO: global tfree
  // if (p.tfreecount >= 64) {
  //   // Move half of local free Ts to gs
  //   gs.tfreelock.lock();
  //   for p.gfreecnt >= 32 {
  //     p.gfreecnt--
  //     gp = p.gfree
  //     p.gfree = gp.schedlink.ptr()
  //     gp.schedlink.set(sched.gfree)
  //     gs.gfree = gp
  //     gs.ngfree++
  //   }
  //   unlock(&gs.gflock)
  // }
}

// Get from gfree list.
// If local list is empty, grab a batch from global list.
T* p_tfreeget(P& p) { // func gfget(_p_ *p) *g
  T* tp = p.tfree;

  if (tp != nullptr) {
    p.tfree = tp->schedlink;
    --p.tfreecount;
  }

  // TODO: Support for gs.tfree
  // retry:
  //   T* tp = p.tfree;
  //   if (tp == nullptr && gs.tfree != nullptr) {
  //     lock(&gs.gflock)
  //     for p.gfreecnt < 32 && gs.gfree != nil {
  //       p.gfreecnt++
  //       tp = gs.gfree
  //       gs.gfree = tp->schedlink.ptr()
  //       gs.ngfree--
  //       tp->schedlink.set(p.gfree)
  //       p.gfree = tp
  //     }
  //     unlock(&gs.gflock)
  //     goto retry
  //   }

  //   if tp != nil {
  //     p.gfree = tp->schedlink.ptr()
  //     p.gfreecnt--
  //     if tp->stack.lo == 0 {
  //       // Stack was deallocated in gfput.  Allocate a new one.
  //       systemstack(func() {
  //         tp->stack, tp->stkbar = stackalloc(_FixedStack)
  //       })
  //       tp->stackguard0 = tp->stack.lo + _StackGuard
  //       tp->stackAlloc = _FixedStack
  //     } else {
  //       if raceenabled {
  //         racemalloc(unsafe.Pointer(tp->stack.lo), tp->stackAlloc)
  //       }
  //       if msanenabled {
  //         msanmalloc(unsafe.Pointer(tp->stack.lo), tp->stackAlloc)
  //       }
  //     }
  //   }

  return tp;
}


// Purge all cached T's from tfree list to the global list.
void p_tfreepurge(P& p) {
  ScopeLock lock(gs.tfreelock);
  while (p.tfreecount != 0) {
    --p.tfreecount;
    T& t = *p.tfree;
    p.tfree = t.schedlink;
    t.schedlink = gs.tfree;
    gs.tfree = &t;
    ++gs.tfreecount;
  }
}


// Associate P and the current M.
void p_acquire(P& p) {
  T& ct = t_get();
  M& m = *ct.m;

  assert(m.p == nullptr ||!"M in use by other P");

  #ifndef NDEBUG
  if (p.m != nullptr || p.status != PIdle) {
    fprintf(stderr, "p_acquire: p.m=%p, p.status=%u\n", p.m, p.status);
    assert(!"invalid P state");
  }
  #endif

  m.p = &p;
  p.m = &m;
  p.status = PRunning;
}

// Disassociate P and the current M.
void p_release(P& p) {

  #ifndef NDEBUG
  T& ct = t_get();
  assert(ct.m != nullptr);
  M& m = *ct.m;
  assert(m.p != nullptr);
  assert(&p == m.p);
  assert(p.m == &m);
  if (p.m != &m || p.status != PRunning) {
    fprintf(
      stderr,
      "p_release: p.m=%p, m=%p ,p.status=%u\n",
      p.m, &m, p.status
    );
    assert(p.m == &m);
    assert(p.status == PRunning);
  }
  #endif /*defined(NDEBUG)*/

  p.m->p = nullptr;
  p.m = nullptr;
  p.status = PIdle;
}


// Hands off P from syscall or locked M.
void p_handoff(P& p) {
  rxlog("p_handoff: TODO");
  exit(3);
}


// Schedules some M to run the p (creates an M if necessary).
// If p==nil, tries to get an idle P, if no idle P's does nothing.
// May run with m.p==null.
// If spinning is set, the caller has incremented nmspinning and startm will
// either decrement nmspinning or set m.spinning in the newly started M.
void p_startm(P* p, bool spinning) {
  rxlog("p_startm: TODO (exiting)");
  exit(3);
}
