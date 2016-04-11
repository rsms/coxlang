#pragma once
#include "atomic.h"
#include "os.h"

// FdMutex is a specialized synchronization primitive
// that manages lifetime of an fd and serializes access
// to read and write methods on NetChan.
struct FdMutex {
  // .state is organized as follows:
  //   1 bit - whether netFD is closed, if set all subsequent lock operations will fail.
  //   1 bit - lock for read operations.
  //   1 bit - lock for write operations.
  //   20 bits - total number of references (read+write+misc).
  //   20 bits - number of outstanding read waiters.
  //   20 bits - number of outstanding write waiters.
  enum State : uint64_t {
    Closed  =                0x1, // 1 << 0
    RLock   =                0x2, // 1 << 1
    WLock   =                0x4, // 1 << 2
    Ref     =                0x8, // 1 << 3
    RefMask =           0x7ffff8, // (1<<20 - 1) << 3
    RWait   =           0x800000, // 1 << 23
    RMask   =      0x7ffff800000, // (1<<20 - 1) << 23
    WWait   =      0x80000000000, // 1 << 43
    WMask   = 0x7ffff80000000000, // (1<<20 - 1) << 43  
  };

  atomic_t(State) state; // = 0
  Sema            rsema;
  Sema            wsema;

  // Read operations must do rwLock(true)/rwUnlock(true).
  // Write operations must do rwLock(false)/rwUnlock(false).
  // Misc operations must do incref/decref.
  //   Misc operations include functions like setsockopt and setDeadline.
  //   They need to use Incref/Decref to ensure that they operate on the
  //   correct fd in presence of a concurrent Close call
  //   (otherwise fd can be closed under their feet).
  // Close operation must do increfAndClose/decref.

  // RWLock/Incref return whether fd is open.
  // RWUnlock/Decref return whether fd is closed and there are no remaining references.
  bool incref();
  bool increfAndClose();
  bool decref();
  bool rwLock(bool read);
  bool rwUnlock(bool read);
};
