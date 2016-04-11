#pragma once

#include <functional>

#if !defined(__STDC_NO_THREADS__)
  #include <thread>
#else
  #error "Missing thread implementation"
#endif

#include "cox.h"
#include "task.h"
#include "funnel.h"
#include "list.h"
#include "event.h"

struct Sched {
  Sched()
    : _rootT{*this}
    , _currT{&_rootT}
  {}

  // Returns the scheduler for the calling thread.
  static Sched& threadLocal();

  // Returns true if the calling thread is the same as this
  // scheduler's thread.
  bool isCurrent() const;

  // Get a pointer to the current task.
  // This can only be called from the scheduler's thread.
  Task& currentTask() const;

  // cancels T owned by S by:
  // 1. Marking the task as canceled.
  // 2. Resuming the task, causing
  // 3. the suspend() call (which put the task in wait mode)
  //    to throw kCanceled, causing either:
  //    a. Task::_fn to catch the exception and begin possibly
  //       asynchronous cancelation, or
  //    b. Task::_fn stack to unwind; Task::main catches the
  //       exception and porpagates cancelation to T's children.
  // 4.a. If cancelation completed, T ends.
  // 4.b. If cancelation was delayed:
  //    1. T is marked as "waiting for children to end".
  //    2. T is suspended.
  //
  // Returns true if the task ended.
  // Returns false if the cancelation was canceled by the task.
  bool cancel(Task& T);

  // Immediately end T and all its children.
  void kill(Task& T);

  // Suspend T and jump back to whatever resume()d T in the first place.
  intptr_t suspend(Task& T, TaskStatus st);

  // Jump from _currT to T, and back again when T is suspended or ends.
  // Returns the status of T, useful to check this for TaskStatus::Ended
  // as T might be invalid (i.e. have been deallocated.)
  TaskStatus resume(Task& T, intptr_t v);

  // Function closure to be passed to async()
  using AsyncFunc = std::function<void(Sched&)>;

  // Perform fn on scheduler's thread
  void async(AsyncFunc&&);

  // Execute tasks that are ready to run.
  bool poll();

  // Ends the root task, canceling all tasks launched in this scheduler which
  // doesn't have any additional TaskHandles.
  // Called automatically for the main scheduler before process exit.
  // Can be called manually. Subsequent invocation has no effect.
  void end();

  int debugReadTCP(Task&);

private:
  friend struct Task;
  friend TaskHandle go(TaskFn&&);
  friend Cond awaitCond(int, Cond);

  // Scheduler thread main function
  void threadMain();

  // Called by Task::main after T's function has completed,
  // but before T has ended.
  void removeChildren(Task&);

  void taskEnded(Task&);

  // Async job
  struct Async {
    volatile Async* _next_link = nullptr;
    AsyncFunc       fn;
  };

  // Used to lazily start a new thread
  volatile long   _running = 0;

  // Thread this scheduler is running on. Empty for main scheduler.
  std::thread     _thread;
  std::thread::id _threadID;

  // number of live (running or waiting) tasks in this Sched
  size_t  _ntasks = 0;

  // Root task and current task
  Task    _rootT;
  Task*   _currT; // initially a ref to _rootT

  // Async calls posted from other threads, to be executed in this thread
  rx::Funnel<Async> _asyncQueue;

  // The run queue contains tasks that are to be resumed immediately.
  // poll() dequeues and resumes tasks in this queue.
  //
  // Example:
  //   _runQ = [A, B, C]
  //   poll()
  //     T = head = A
  //     while (T) {
  //       run(T)
  //       if T ended:
  //         nextT = T.next
  //         _runQ.remove(T)  // clears .next and .prev
  //         T = nextT
  //     }
  //
  List<Task> _runQ;

  // Number of tasks waiting for a condition.
  // Note: These tasks are not in _runQ, so the total number of
  // tasks in this scheduler is _runQ.size() + _numWaiting.
  unsigned int _numWaiting = 0;

  // Host event system
  Events _events;
};


inline bool Sched::isCurrent() const {
  return _threadID == std::this_thread::get_id();
}

inline Task& Sched::currentTask() const {
  assert(_currT != nullptr);
  return *_currT;
}
