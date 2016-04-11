#pragma once

// I/O conditions
enum Cond {
  CondNone    = 0,
  CondIORead  = 2,
  CondIOWrite = 4,
  CondEOF     = 8,
  CondErr     = 16,
  CondCancel  = 32, // operation canceled (task being killed or canceled)
};
