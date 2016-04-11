// T – task
#pragma once
#include "atomic.h"
#include <functional>
#include <boost/context/stack_context.hpp>

struct T;

using TFun       = std::function<void()>;
using TUnlockFun = bool(*)(T&,intptr_t);

enum TStatus {
  TIdle       = 0,
  TRunnable, // 1 runnable and on a run queue
  TRunning,  // 2
  TSyscall,  // 3
  TWaiting,  // 4
  TDead,     // 5
};

// raised inside a task that's being canceled
struct TCancel {};

// task stack memory
struct TStackMem {
  void*    p;
  uint32_t size;
};

struct M;

struct T {
  uint64_t          ident;     // global unique identifier
  M*                m;
  M*                lockedm;
  void*             stackctx;  // execution context (stack, regs)
  void*             stackp;    // stack memory base
  size_t            stacksize; // size of stackp in bytes
  T*                parentt;   // task that spawned this task. null if unlinked.
  T*                schedlink; // next task to be scheduled
  atomic_t(TStatus) atomicstatus;
  int64_t           waitsince;  // approx time when the T become blocked
  TFun              fn;
};

// Returns the current T
T& t_get();

// Puts the current task into a waiting state and calls unlockf(t, unlockv)
// where t is the current task when calling t_park.
// If unlockf returns false, the task is resumed.
void t_park(TUnlockFun unlockf, intptr_t unlockv, const char* reason);

// Mark t ready to run, adding it to t_get().m.runq. Opposite of t_park().
void t_ready(T& t);

// Saves fromt state, then executes tot on fromt.m
void t_switch(T& fromt, T& tot);

TStatus t_readstatus(T&);
void t_casstatus(T&, TStatus oldval, TStatus newval);


void go2(TFun fn);


// ——————————————————————————————————————————————————————————————————————————————
// impl

#ifdef __GNUC__
  #define thread_local __thread
#elif __STDC_VERSION__ >= 201112L
  #define thread_local _Thread_local
#elif defined(_MSC_VER)
  #define thread_local __declspec(thread)
#else
  #error "no support for thread-local storage"
#endif

extern thread_local T* _tlt;

inline __attribute__((always_inline)) T& t_get() {
  // Force inline to allow call-site TLS load optimizations.
  // Example:
  //   void foo() {
  //     int s = 0;
  //     for (size_t i = 10; i != 0; ++i) {
  //       s += t_get().x;
  //     }
  //   }
  // If t_get would not be inline, we would perform 10 TLS loads here, but
  // making t_get inline, the compiler optimizes the TLS load by moving it
  // outside of the loop, effectively:
  //   void foo() {
  //     int s = 0;
  //     auto& tmp = t_get();
  //     for (size_t i = 10; i != 0; ++i) {
  //       s += tmp.x;
  //     }
  //   }
  return *_tlt;
}

inline TStatus t_readstatus(T& t) {
  return AtomicLoad(&t.atomicstatus);
}
