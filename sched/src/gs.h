#pragma once
#include "atomic.h"
#include <mutex>

struct T;
struct M;
struct P;

// The max value of GOMAXPROCS.
// There are no fundamental restrictions on the value.
constexpr uint32_t MaxMaxprocs = 256;

// Global scheduler state
struct GS {
  atomic_t(uint64_t) tidgen = 0;   // next t.ident
  atomic_t(uint64_t) lastpoll = 0; // time of last poll, or 0 if never polled

  std::mutex   lock; // protects access to runq et al

  // Ms
  M*           midle = nullptr;  // idle m's waiting for work
  int32_t      midlecount = 0;   // number of idle m's waiting for work
  int32_t      nmidlelocked = 0; // number of locked m's waiting for work
  int32_t      mcount = 0;       // number of m's that have been created
  int32_t      maxmcount;        // maximum number of m's allowed (or die)

  // Ps
  P*                 allp[MaxMaxprocs + 1]; // pointers to null or Ps
  atomic_t(uint32_t) maxprocs = 0;    // max active Ps
  P*                 pidle = nullptr; // idle p's
  atomic_t(uint32_t) pidlecount = 0;
  atomic_t(int32_t)  nmspinning = 0; // See "Worker thread parking/unparking" in docs

  // Ts – global cache of dead T's
  std::mutex tfreelock;
  T*         tfree = nullptr;
  uint32_t   tfreecount = 0;

  // Global runnable queue
  atomic_t(T*) runqhead = nullptr;
  atomic_t(T*) runqtail = nullptr;
  uint32_t     runqsize = 0;

  // TODO: T freelist
};

extern GS gs;
extern M* m0;

// Try get a batch of G's from the global runnable queue.
// gs must be locked.
T* gs_runqget(P&, uint32_t max);

// Put T in the global runnable queue tail. gs must be locked.
void gs_runqput(T&);

// Put T in global runnable queue head. gs must be locked.
void gs_runqputhead(T&);

// Injects the list of runnable T's into the scheduler
void gs_runqinject(T* tlist);

// Finds a runnable goroutine to execute.
// Tries to steal from other P's, get g from global queue, poll network.
T* gs_findrunnable(bool& inheritTime);

// {Try get, Put} P from gs.pidle list. gs must be locked.
P* gs_pidleget();
void gs_pidleput(P&);

// {Try get, Put} M on midle list. gs must be locked.
M* gs_midleget();
void gs_midleput(M&);

// Tries to add one more P to execute G's.
// Called when a G is made runnable (newproc, ready).
void gs_wakep();

void gs_bootstrap();
void gs_maincancel();
