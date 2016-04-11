#include "fdmutex.h"
#include "common.h"
#include "log.h"

// impl in go runtime:
// func runtime_Semacquire(sema *uint32)
// func runtime_Semrelease(sema *uint32)

// returns whether fd is open.
bool FdMutex::incref() {
  TODO_IMPL;
  return false;
  // for {
  //   old := atomic.LoadUint64(&mu.state)
  //   if old&mutexClosed != 0 {
  //     return false
  //   }
  //   new := old + mutexRef
  //   if new&mutexRefMask == 0 {
  //     panic("net: inconsistent fdMutex")
  //   }
  //   if atomic.CompareAndSwapUint64(&mu.state, old, new) {
  //     return true
  //   }
  // }
}

// returns whether fd is open.
bool FdMutex::increfAndClose() {
  State old = AtomicLoad(&state);
  while (1) {
    if (old & Closed) {
      return false;
    }
    // Mark as closed and acquire a reference.
    auto nev = State((old | Closed) + Ref);
    if (!(nev & RefMask)) {
      panic("inconsistent state");
    }
    // Remove all read and write waiters.
    nev = State(nev & ~(RMask | WMask));
    if (AtomicCasRelAcq(&state, &old, nev)) {
      // Wake all read and write waiters,
      // they will observe closed flag after wakeup.
      while (old & RMask) {
        old = State(old - RWait);
        TODO_SECTION; // TODO: unpark task waiting for rsema
        // See semrelease(addr *uint32) in runtime/sema.go
        // runtime_Semrelease(rsema);
      }
      while (old & WMask) {
        old = State(old - WWait);
        TODO_SECTION; // TODO: unpark task waiting for wsema
        // See semrelease(addr *uint32) in runtime/sema.go
        // runtime_Semrelease(wsema);
      }
      return true;
    }
  }
}

// returns whether fd is closed and there are no remaining references.
bool FdMutex::decref() {
  State old = AtomicLoad(&state);
  while (1) {
    if (!(old & RefMask)) {
      panic("inconsistent state");
    }
    auto nev = State(old - Ref);
    if (AtomicCasRelAcq(&state, &old, nev)) {
      return (nev & (Closed | RefMask)) == Closed;
    }
  }
}

// returns whether fd is open.
bool FdMutex::rwLock(bool read) {
  TODO_IMPL;
  return false;
  // var mutexBit, mutexWait, mutexMask uint64
  // var mutexSema *uint32
  // if read {
  //   mutexBit = mutexRLock
  //   mutexWait = mutexRWait
  //   mutexMask = mutexRMask
  //   mutexSema = &mu.rsema
  // } else {
  //   mutexBit = mutexWLock
  //   mutexWait = mutexWWait
  //   mutexMask = mutexWMask
  //   mutexSema = &mu.wsema
  // }
  // for {
  //   old := atomic.LoadUint64(&mu.state)
  //   if old&mutexClosed != 0 {
  //     return false
  //   }
  //   var new uint64
  //   if old&mutexBit == 0 {
  //     // Lock is free, acquire it.
  //     new = (old | mutexBit) + mutexRef
  //     if new&mutexRefMask == 0 {
  //       panic("net: inconsistent fdMutex")
  //     }
  //   } else {
  //     // Wait for lock.
  //     new = old + mutexWait
  //     if new&mutexMask == 0 {
  //       panic("net: inconsistent fdMutex")
  //     }
  //   }
  //   if atomic.CompareAndSwapUint64(&mu.state, old, new) {
  //     if old&mutexBit == 0 {
  //       return true
  //     }
  //     runtime_Semacquire(mutexSema)
  //     // The signaller has subtracted mutexWait.
  //   }
  // }
}

// returns whether fd is closed and there are no remaining references.
bool FdMutex::rwUnlock(bool read) {
  TODO_IMPL;
  return false;
  // var mutexBit, mutexWait, mutexMask uint64
  // var mutexSema *uint32
  // if read {
  //   mutexBit = mutexRLock
  //   mutexWait = mutexRWait
  //   mutexMask = mutexRMask
  //   mutexSema = &mu.rsema
  // } else {
  //   mutexBit = mutexWLock
  //   mutexWait = mutexWWait
  //   mutexMask = mutexWMask
  //   mutexSema = &mu.wsema
  // }
  // for {
  //   old := atomic.LoadUint64(&mu.state)
  //   if old&mutexBit == 0 || old&mutexRefMask == 0 {
  //     panic("net: inconsistent fdMutex")
  //   }
  //   // Drop lock, drop reference and wake read waiter if present.
  //   new := (old &^ mutexBit) - mutexRef
  //   if old&mutexMask != 0 {
  //     new -= mutexWait
  //   }
  //   if atomic.CompareAndSwapUint64(&mu.state, old, new) {
  //     if old&mutexMask != 0 {
  //       runtime_Semrelease(mutexSema)
  //     }
  //     return new&(mutexClosed|mutexRefMask) == mutexClosed
  //   }
  // }
}
