#include "task.h"
#include "sched.h"

// static void park_m(Task* t) {
//   casgstatus(gp, _Grunning, _Gwaiting)
//   dropg()

//   if _g_.m.waitunlockf != nil {
//     fn := *(*func(*g, unsafe.Pointer) bool)(unsafe.Pointer(&_g_.m.waitunlockf))
//     ok := fn(gp, _g_.m.waitlock)
//     _g_.m.waitunlockf = nil
//     _g_.m.waitlock = nil
//     if !ok {
//       if trace.enabled {
//         traceGoUnpark(gp, 2)
//       }
//       casgstatus(gp, _Gwaiting, _Grunnable)
//       execute(gp, true) // Schedule it back, never returns.
//     }
//   }
//   schedule()
// }

// // Puts the current task into a waiting state and calls unlockf.
// // If unlockf returns false, the task is resumed.
// // TODO: Move to task source
// bool task_park(UnlockFun unlockf, void* lock, const char* reason) {
//   Task& T = task_self();
//   assert(T.m != nullptr);
//   M& m = *T.m;

//   m.waitUnlockF = unlockf;
//   m.waitlock = lock

//   m.call(park_m);

//   // TODO: handle task cancelation:
//   // either catch & rethrow, or put something on the stack that
//   // when deallocated removes any event observer.
//   return (Cond)T._S.suspend(T, TaskStatus::Waiting);
// }

// void task_park(bool unlockf*(T* t, T*), lock unsafe.Pointer, reason string) {
//   mp := acquirem()
//   gp := mp.curg
//   status := readgstatus(gp)
//   if status != _Grunning && status != _Gscanrunning {
//     throw("gopark: bad g status")
//   }
//   mp.waitlock = lock
//   mp.waitunlockf = *(*unsafe.Pointer)(unsafe.Pointer(&unlockf))
//   gp.waitreason = reason
//   mp.waittraceev = traceEv
//   mp.waittraceskip = traceskip
//   releasem(mp)
//   // can't do anything that might move the G between Ms here.
//   mcall(park_m)
// }
