#include "t.h"
#include "m.h"
#include "p.h"
#include "gs.h"
#include "stack.h"
#include "log.h"

#include <boost/context/protected_fixedsize_stack.hpp>
#include <boost/context/stack_traits.hpp>

extern "C" {
  void* make_fcontext(void* sp, size_t sz, void(*fn)(intptr_t));
  intptr_t jump_fcontext(void* outfc, void* infc, intptr_t v, bool savefpu);
}

// Thrown in a task to cancel or kill the task
struct _TKill {};
static _TKill  kTKill;
static TCancel kTCancel;

thread_local T* _tlt = nullptr; // current task


void t_casstatus(T& t, TStatus oldval, TStatus newval) {
  assert(oldval != newval);
  while (!AtomicCasRelAcq(&t.atomicstatus, &oldval, newval)) {
    assert(oldval != TWaiting || t.atomicstatus != TRunnable
           ||!"waiting for TWaiting but is TRunnable");
  }
}

struct TMainData { T& t; TFun& fnp; };

// Saves fromt state, then executes tot on fromt.m
void t_switch(T& fromt, T& tot) {
  tot.waitsince = 0;
  tot.m = fromt.m;
  tot.m->curt = &tot;
  _tlt = &tot;
  jump_fcontext(&fromt.stackctx, tot.stackctx, 0, 1);
}

// dropg removes the association between m and the current m->curt (ct for short).
// Typically a caller sets ct's status away from Grunning and then
// immediately calls dropg to finish the job. The caller is also responsible
// for arranging that ct will be restarted using ready at an
// appropriate time. After calling dropg and arranging for ct to be
// readied later, the caller can do other work but eventually should
// call schedule to restart the scheduling of goroutines on this m.
void t_dropm(T& ct) {
  M& m = *ct.m;
  if (m.lockedt == nullptr) {
    assert(m.curt == &ct);
    ct.m = nullptr;
    m.curt = nullptr;
  }
}

// WTF does this do?
// How can it do _g_.m.curg.m=nil followed by _g_.m.curg=nil, when _g_==_g_.m.curg?!
// func dropg() {
//   _g_ := getg()
//   if _g_.m.lockedg == nil {
//     _g_.m.curg.m = nil
//     _g_.m.curg = nil
//   }
// }


static void t_park_m(M& m, T& t) {
  rxlog("t_park_m T@" << &t << ", m.curt=" << m.curt);
  //T& ct = *m.curt; // faster than t_get()

  t_casstatus(t, TRunning, TWaiting);

  // Disassociate M from t
  // t_dropm(t); // broken

  if (m.waitunlockf != nullptr) {
    bool contwait = m.waitunlockf(t, m.waitunlockv);
    m.waitunlockf = nullptr;
    m.waitunlockv = 0;
    if (!contwait) {
      // unpark
      t_casstatus(t, TWaiting, TRunnable);
      m_execute(m, t, /*inheritTime=*/true); // Schedule it back, never returns.
    }
  }

  // t is still waiting -- schedule another runnable task, poll network, etc
  m_schedule(m);
}

// Puts the current task into a waiting state and calls unlockf(t, unlockv)
// where t is the current task when calling t_park.
// If unlockf returns false, the task is resumed.
void t_park(TUnlockFun unlockf, intptr_t unlockv, const char* reason) {
  T& t = t_get();
  rxlog("t_park: " << reason << " T@" << &t);

  assert(t_readstatus(t) == TRunning);

  t.m->waitunlockf = unlockf;
  t.m->waitunlockv = unlockv;

  m_call(t, t_park_m);
}

// Mark t ready to run, adding it to current M runq. Opposite of t_park().
void t_ready(T& t) {
  T& ct = t_get();

  assert(t_readstatus(t) != TWaiting);
  t_casstatus(t, TWaiting, TRunnable);

  // Put in runqueue and make it the next task to be executed
  p_runqput(*ct.m->p, t, /*next=*/true);

  // TODO: M/P expansion if busy:
  // if AtomicLoad(&sched.npidle) != 0 && AtomicLoad(&sched.nmspinning) == 0 {
  //   // TODO: fast atomic
  //   wakep();
  // }
}

// effective parent task for t
// static inline T* t_parent(T& t) {
//   return t.parentt == nullptr ? &t.m->t0 : t.parentt;
// }

// task entry point
static void tmain(intptr_t v) {
  T& t = *((T*)v);
  
  try {
    t.fn();
    rxlog("tmain: T@" << &t << " died: exit");
  } catch (TCancel&) {
    rxlog("tmain: T@" << &t << " died: canceled");
    // T._cancel = Task::Cancelation::Canceled;
  } catch (_TKill&) {
    rxlog("tmain: T@" << &t << " died: killed");
    // assert(T._cancel == Task::Cancelation::Killed);
  } catch (...) {
    rxlog("tmain: T@" << &t << " died: exception");
    // TODO: Kill t, and perhaps pass the exception to its parent.
    // std::current_exception() -> std::exception_ptr
  }

  AtomicStore(&t.atomicstatus, TDead);

  // Add dead t to P's tfree list
  p_tfreeput(*t.m->p, t);

  // Note: Oportunity for optimization: We could use a version of jump_fcontext
  // here that only restores context (parent), but doesn't save since this
  // current stack is never going to be visited again. This would also allow
  // us to reclaim the T resource right now, instead of queueing it up to be
  // reclaimed by schedule() (since we need t.stackctx to be valid in t_run().)
  T& pt = (t.parentt == nullptr) ? t.m->t0 : *t.parentt;
  t_switch(t, pt);
}


static inline uint64_t t_idgen() {
  return AtomicXAdd(&gs.tidgen, 1);
}


// Allocate a new T, with a stack big enough for stacksize bytes.
static T* t_alloc() {
  T* tp = (T*)calloc(1, sizeof(T));
  if (tp == nullptr) {
    fprintf(stderr, "out of memory");
    abort();
  }
  tp->ident    = t_idgen();
  tp->stackp   = stack_alloc(0, tp->stacksize);
  tp->stackctx = make_fcontext(tp->stackp, tp->stacksize, &tmain);
  return tp;
}


void go2(TFun fn) {
  T& ct = t_get();
  M& m = *ct.m;

  rxlog("go2: t_get() = " << &ct);
  rxlog("go2: m0->t0  = " << &m0->t0);
  
  // reuse old dead task or allocate new
  T* tp = p_tfreeget(*m.p);
  if (tp == nullptr) {
    tp = t_alloc();
  }

  T& t = *tp;
  t.parentt      = &ct;
  t.schedlink    = nullptr;
  t.waitsince    = 0;
  t.fn           = fn;
  t.atomicstatus = TRunning;

  // TODO: probably put ct on runq as we set it to TRunnable.
  // TODO: disassociate M from ct?
  // TODO: call m_execute here instead?

  assert(t_readstatus(ct) == TRunning);
  t_casstatus(ct, TRunning, TRunnable);

  t.m = ct.m;
  t.m->curt = &t;
  _tlt = &t;

  auto r = jump_fcontext(&ct.stackctx, t.stackctx, (intptr_t)&t, 1);

  if (t.atomicstatus == TDead) {
    rxlog("go2: returned (dead)");
    return;
  }

  if (&ct == &m.t0) {
    rxlog("go2: returned to t0");
    if (r != 0) {
      // jump_fcontext returned mcall function, or nothing
      auto pp = (void**)r;
      auto fn = (MCallFun)pp[0];
      auto tp = (T*)pp[1];
      rxlog("t0 executing mcall with T@" << tp);
      fn(m, *tp);
    }
  } else {
    rxlog("go2: switch to T@" << &ct << " from T@" << &t << " r=" << r);
  }
}


// We could reuse this old code to implement a growing stack by
// checking BP/SP and compare it to t.stackmem.sp before resuming a task,
// and grow t.stackmem.sp if needed.
//
// void* t_get2() {
//   struct { void* sp; void* bp; } v{(void*)0xb33f,(void*)0xb33f};
//   auto vp = &v;
//   __asm__ __volatile__ (
//     // "leaq 1f(%%rip), %%rax\n\t"
//     "movq %%rsp, (%0)\n"
//     "movq %%rbp, 8(%0)\n"
//     : "=r" (vp)
//     // : no input
//     // : "rax", "rcx", "rdx"
//     //: "rax", "rcx", "rdx", "r8", "r9", "r10", "r11"//, "memory", "cc"
//   );
//   rxlog("t_get2: sp=" << v.sp << ", bp=" << v.bp);
//   return v.sp;
// }
