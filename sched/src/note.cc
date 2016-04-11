#include "note.h"
#ifndef NDEBUG
  #include "t.h"
#endif
#include "m.h"
#include "os.h"
#include "common.h"
#include <stdint.h>
#include <assert.h>

static constexpr uintptr_t kLocked{UINTPTR_MAX};


void note_clear(Note& n) {
  n.key = 0;
}


void note_sleep(Note& n, M& m) {
  #ifndef NDEBUG
  T& t = t_get();
  assert(&t == &t.m->t0 /* else: not on t0 */);
  #endif

  uintptr_t expect = 0;
  if (!AtomicCasRelAcq(&n.key, &expect, (uintptr_t)&m)) {
    // Must be locked (got sema_wake).
    if (expect != kLocked) {
      panic("m_wait out of sync");
    }
    return;
  }

  // Queued.  Sleep.
  m.blocked = true;
  sema_create(m.waitsema, 0); // noop if already created
  sema_sleep(m.waitsema, -1); // -1 = no deadline, sleep until woken.
  m.blocked = false;
}


void note_wake(Note& n) {
  uintptr_t v = AtomicLoad(&n.key);
  while (!AtomicCasRelAcq(&n.key, &v, kLocked)) {
  }

  // Successfully set waitm to locked.
  // What was it before?
  switch (v) {
  case 0:
    // Nothing was waiting. Done.
    break;
  case kLocked:
    // Two notewakeups!  Not allowed.
    panic("double wake");
  default:
    // Must be the waiting M.  Wake it up.
    M& m = *((M*)v);
    sema_wake(m.waitsema);
  }
}
