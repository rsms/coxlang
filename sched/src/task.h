#pragma once
/*
A Task is referenced by:
- Its parent task, until the parent task ends
- Any user TaskHandle

Example scenarios:

A task ends while it's referenced by parent or handle:

start              end
    |————<runs>————|           delete
    |———————<referenced>———————|

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A task loses parent and handle references while running:

start               cancel   end
    |———————<runs>———————|---|
    |————<referenced>————|---|
                             delete

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A task is canceled while being referenced by parent:

start                   cancel   end
    |——————————<runs>————————|---|
    |————————<referenced>————————|
                                 delete

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A task is canceled while being referenced by parent and handle:

start                   cancel   end
    |——————————<runs>————————|---|       delete
    |————————————<referenced>————————————|

*/
#include "atomic.h"
#include "list.h"

#include <boost/context/fcontext.hpp>
#include <boost/context/stack_context.hpp>

#include <set>

namespace ctx = boost::context;

struct M;

using TaskFn = std::function<void()>;
using TaskID = uint64_t;

enum TaskStatus : intptr_t {
  Init = 0,
  Running,
  Yielding,
  Waiting,
  Ending,
  Ended,
};

struct Sched;

struct Task {
  Task(Sched& S, TaskID ident, Task* parentT, TaskFn&& fn)
    : _S{S}
    , _id{ident}
    , _fn{std::forward<decltype(fn)>(fn)}
    , _parent{parentT}
    , _prev_link{nullptr}
    , _next_link{nullptr}
  {}

  // Root task
  explicit Task(Sched& S)
    : _S{S}
    , _id{0}
    , _status{TaskStatus::Running}
    , _fc{nullptr}
    , _parent{nullptr}
    , _prev_link{nullptr}
    , _next_link{nullptr}
  {}

  void initCtx(ctx::stack_context sc) {
    _sc = sc;
    _fc = ctx::make_fcontext(_sc.sp, _sc.size, Task::main);
  }

  static void main(intptr_t v);

  TaskID ident() const { return _id; }

  Task* retainRef() {
    rx_atomic_add32(&_refcount, 1);
    return this;
  }

  bool hasOneRef() {
    rx_atomic_barrier();
    return _refcount == 1;
  }

  bool releaseRef() {
    if (rx_atomic_sub_fetch(&_refcount, 1) == 0) {
      lostAllHandles();
      return true;
    }
    return false;
  }

  void lostAllHandles();
  void removeChildren();

  // Function context of the environment that started this task.
  // Note that this might not be the same as Task::_parent, i.e. when
  // Task::_parent is in a different scheduler.
  ctx::fcontext_t parentCtx();

  M*                 m = nullptr;

  Sched&             _S; // never null
  TaskID             _id;
  TaskFn             _fn;

  // State switches from Init, to intermediate state and eventually
  // settles at Completed.
  volatile TaskStatus _status = TaskStatus::Init;

  // Stack and Function context for this task
  ctx::stack_context _sc;
  ctx::fcontext_t    _fc;

  // Parent task which launched this task.
  // Note: This is not neccesarily the task we switch to when
  // this task is being suspended.
  Task*              _parent;

  // Live child tasks which were started by this task.
  // Note that a child might outlive a parent if user code holds a
  // TaskHandle to it, meaning this might not be empty when this
  // task ends.
  std::set<Task*>    _children;

  // Links used intrusive List<Task>
  Task*              _prev_link;
  Task*              _next_link;

  // Link to next task to be scheduled after this tasks stops
  Task*              _schedlink;

  // Cancelation status
  enum class Cancelation {
    // Task has not yet been canceled
    NotCanceled = 0,

    // Canceled but might be waiting for children.
    // a) Issued when the parent task exited cleanly or was itself canceled.
    // b) Issued when a task is explicitly canceld by user.
    Canceled = 1,

    // A Killed task ends immediately.
    // a) Issued when the parent task is being killed ("forcefully canceled"),
    //    which might happen either when a fatal error occurs or,
    //    when a cancelation deadline is reached.
    Killed = 2,
  };
  Cancelation _cancel = Cancelation::NotCanceled;

  // Number of references held to this task.
  // When this drops to zero, the task is canceled.
  volatile uint32_t _refcount = 0;
};
