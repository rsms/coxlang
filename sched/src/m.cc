#include "m.h"
#include "p.h"
#include "gs.h"
#include "log.h"
#include "common.h"
#include <assert.h>

using ScopeLock = std::lock_guard<std::mutex>;

extern "C" {
  intptr_t jump_fcontext(void* outfc, void* infc, intptr_t v, bool savefpu);
}


M::M() : curt{&t0} {
  t0.m = this;
  t0.stackctx = nullptr;
  t0.stackp = nullptr;
  t0.stacksize = 0;
  t0.schedlink = nullptr;
  t0.atomicstatus = TRunning;
}

// Switch to m.t0's stack and call fn(m,t)
void m_call(T& ct, MCallFun fn) {
  void* pp[] = {(void*)fn, (void*)&ct};
  T& t0 = ct.m->t0;
  ct.m->curt = &t0;
  t0.m = ct.m;
  _tlt = &t0;
  jump_fcontext(&ct.stackctx, t0.stackctx, (intptr_t)&pp, 1);
}

// Schedules gp to run on the current M.
// If inheritTime is true, gp inherits the remaining time in the
// current time slice. Otherwise, it starts a new time slice.
// Never returns.
void m_execute(M& m, T& t, bool inheritTime) {
  rxlog("m_execute: T@" << &t);
  t_casstatus(t, TRunnable, TRunning);
  t.waitsince = 0;

  if (!inheritTime) {
    ++m.p->schedtick;
  }

  assert(m.curt != nullptr);
  //T& ct = *m.curt;
  T& ct = t_get();
  _tlt = &t;
  m.curt = &t;
  t.m = &m;

  jump_fcontext(&ct.stackctx, t.stackctx, (intptr_t)0, 1);
}

// Check for deadlock situation.
// The check is based on number of running M's, if 0 -> deadlock.
static void checkdead() {
  rxlog("checkdead: TODO (not implemented)");
}

static void incidlelocked(int32_t v) {
  ScopeLock lock(gs.lock);
  gs.nmidlelocked += v;
  if (v > 0) {
    checkdead();
  }
}

// Stops execution of the current M (m) that is locked to a T until the T is
// runnable again. Returns with acquired P.
static void m_stoplocked(M& m) {
  assert(m.lockedt != nullptr && m.lockedt->lockedm == &m /*inconsistent locking*/);
  if (m.p != nullptr) {
    // Schedule another M to run this P.
    p_release(*m.p);
    p_handoff(*m.p);
  }
  incidlelocked(1);
  // Wait until another thread schedules lockedt again.
  rxlog("m_stoplocked: TODO (exiting)");
  exit(3);
  // note_sleep(&m.park);
  // note_clear(&m.park);
  // assert(t_readstatus(*m.lockedt) == TRunnable);
  // p_acquire(*m.nextp);
  // m.nextp = nullptr;
}

// Schedules the locked M to run the locked T.
static void m_startlocked(T& t) {
  rxlog("m_startlocked: TODO (exiting)");
  exit(3);
}

static void m_resetspinning(M& m) {
  assert(m.spinning);
  m.spinning = false;
  auto nmspinning = AtomicXAdd(&gs.nmspinning, -1);
  assert(nmspinning >= 0 /*else: negative nmspinning*/);
  // M wakeup policy is deliberately somewhat conservative, so check if we
  // need to wakeup another P here. See "Worker thread parking/unparking"
  // comment at the top of the file for details.
  if (nmspinning == 1 && AtomicLoad(&gs.pidlecount) > 0) {
    gs_wakep();
  }
}


// One round of scheduler: find a runnable goroutine and execute it.
// Never returns.
void m_schedule(M& m) {
  rxlog("m_schedule");

  assert(m.locks == 0 /* else: holding locks */);
  assert(m.p != nullptr);

  if (m.lockedt != nullptr) {
    m_stoplocked(m);
    m_execute(m, *m.lockedt, /*inheritTime=*/false); // never returns
  }

top:

  T* tp = nullptr;
  bool inheritTime = false;
  
  // Check the global runnable queue once in a while to ensure fairness.
  // Otherwise two goroutines can completely occupy the local runqueue
  // by constantly respawning each other.
  if (m.p->schedtick % 61 == 0 && gs.runqsize > 0) {
    rxlog("m_schedule: gs_runqget");
    ScopeLock lock(gs.lock);
    tp = gs_runqget(*m.p, 1);
  }

  // local runnable queue
  if (tp == nullptr) {
    rxlog("m_schedule: p_runqget");
    tp = p_runqget(*m.p, inheritTime);
    assert(tp == nullptr || !m.spinning /*spinning with local work*/);
  }

  if (tp == nullptr) {
    rxlog("m_schedule: gs_findrunnable");
    tp = gs_findrunnable(inheritTime); // blocks until work is available
  }

  // This thread is going to run a goroutine and is not spinning anymore,
  // so if it was marked as spinning we need to reset it now and potentially
  // start a new spinning M.
  if (m.spinning) {
    m_resetspinning(m);
  }

  if (tp->lockedm != nullptr) {
    // Hands off own P to the locked M,
    // then blocks waiting for a new P.
    m_startlocked(*tp);
    goto top;
  }

  m_execute(m, *tp, inheritTime); // never returns
}


void m_deadqadd(M& m, T& t) {
  if (m.deadq != nullptr) {
    t.schedlink = m.deadq;
  }
  m.deadq = &t;
}


uint32_t m_fastrand(M& m) {
  uint32_t x = m.fastrand;
  x += x;
  uint32_t y = x ^ 0x88888eef;
  return m.fastrand = (y < x) ? x : y;
  // if (x & 0x80000000L) {
  //   x ^= 0x88888eefUL;
  // }
  // amd64 asm:
  // TEXT fastrand1(SB), NOSPLIT, $0-4
  //   get_tls(CX)
  //   MOVQ  g(CX), AX           // A = g
  //   MOVQ  g_m(AX), AX         // A = g.m
  //   MOVL  m_fastrand(AX), DX  // D = m.fastrand
  //   ADDL  DX, DX              // D = D + D
  //   MOVL  DX, BX              // B = D
  //   XORL  $0x88888eef, DX     // D = D ^ 0x88888eef
  //   CMOVLMI BX, DX            // if (D < B) D = B
  //   MOVL  DX, m_fastrand(AX)  // m.fastrand = D
  //   MOVL  DX, ret+0(FP)       // return D
  //   RET
}


// Stops execution of the current m until new work is available.
// Returns with acquired P.
void m_stop(M& m) {
  #ifndef NDEBUG
  T& ct = t_get();
  assert(((M*)&m) != nullptr);
  assert(ct.m == &m);
  #endif

  if (m.locks != 0) {
    panic("holding locks");
  }
  if (m.p != 0) {
    panic("holding p");
  }
  if (m.spinning) {
    panic("spinning");
  }

  { ScopeLock lock(gs.lock);
    gs_midleput(m);
  }

  // Sleep m until notified
  note_sleep(m.parknote, m);
  note_clear(m.parknote);

  // Acquire P (m.nextp should have been set by whoever woke us with note_wake())
  p_acquire(*m.nextp);
  m.nextp = nullptr;
}

