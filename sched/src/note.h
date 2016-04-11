// One-time notifications
#pragma once
#include "atomic.h"
struct M;

struct Note {
  // key holds:
  // a) nullptr when unused.
  // b) pointer to a sleeping M.
  // c) special internally-known value to indicate locked state.
  atomic_t(uintptr_t) key = 0;
};

// Reset note
void note_clear(Note&);

// Wait for notification, potentially putting M to sleep
// until note_wake is called for the same note.
void note_sleep(Note&, M&);

// Notify
void note_wake(Note&);
