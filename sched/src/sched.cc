#include "sched.h"

#include "common.h"
#include "funnel.h"
#include "log.h"
#include "atomic.h"
#include "taskq.h"
#include "task.h"
#include "list.h"
#include "status.h"
#include "netpoll.h"

#include <boost/context/fcontext.hpp>
#include <boost/context/protected_fixedsize_stack.hpp>
#include <boost/context/stack_context.hpp>
#include <boost/context/stack_traits.hpp>

#include <err.h>
#include <sysexits.h>
#include <stdio.h>
#include <signal.h>
#include <iostream>
#include <functional>

// Thread-local storage support
#if defined(__GNUC__) && !defined(__has_feature) || __has_feature(cxx_thread_local)
  #define TLS_ATTR __thread
  // See also C11 _Thread_local
#elif defined(_MSC_VER)
  #define TLS_ATTR __declspec(thread)
#elif !defined(_WIN32)
  #define TLS_PTHREAD
  #include <pthread.h>
#else
  #error "Missing support for Thread-Local Storage"
#endif

namespace ctx = boost::context;

// NextTaskID returns a process-global address-sized identifier
static volatile TaskID _NextTaskID = 0;
static TaskID NextTaskID() {
  auto tid = (TaskID)rx_atomic_add_fetch(&_NextTaskID, 1);
  if (tid == 0) {
    // overflow to 0 -- 0 has special meaning: root task.
    tid = (TaskID)rx_atomic_add_fetch(&_NextTaskID, 1);
  }
  return tid;
}

// Thrown when a task is killed and is library-private as
// it should not be catchable.
struct _Killed {};
static _Killed kKilled;

// Task stack allocator
static ctx::protected_fixedsize_stack
StackAlloc(ctx::stack_traits::default_size());

// Process-global array of Schedulers initialized by Sched::threadLocal()
static Sched* gScheds = nullptr; // i.e. Sched[]
static size_t gSchedsSize = 1;

// Function context of the environment that started this task.
// Note that this might not be the same as Task::_parent, i.e. when
// Task::_parent is in a different scheduler.
inline ctx::fcontext_t Task::parentCtx() {
  assert(_parent != nullptr); // invalid to call this function on root task
  return (&_parent->_S == &_S) ? _parent->_fc : _S._rootT._fc;
}


void Sched::async(AsyncFunc&& fn) {
  if (_asyncQueue.push(new Async{nullptr, fwd(fn)})) {
    // Queue was empty
    if (_running == 0L && rx_atomic_cas_bool(&_running, 0L, 1L)) {
      // Lazily launch this scheduler's thread.
      // Note: This is thread-safe as Funnel::push guarantees exactly one
      // caller is returned `true`, and since we dequeue with threadMain
      // which isn't running yet, it should be impossible to receive `true`
      // from push on two different calling threads before _running has been
      // set to true. Also, _true is volatile so R/W shouldn't be reordered.
      _thread = std::thread{std::bind(&Sched::threadMain, this)};
      _threadID = _thread.get_id();
    } else {
      rxlog("S.async: TODO: queue empty. signal needs dequeue");
    }
  }
}


bool Sched::poll() {
  rxlog("poll");

  // Any async jobs?
  // These might add to _runQ, so we need to run this before visiting _runQ.
  // Note: Although _asyncQueue is thread-safe (and thus incurs CAS) for
  // push(), pop() is non-thread safe and thus fast and cheap.
  // The common case should be that _asyncQueue is empty.
  Async* a = _asyncQueue.pop();
  if (a != nullptr) {
    a->fn(*this);
    delete a;
  }

  // if (netpoll_active()) {
  //   rxlog("sched poll: netpoll_poll");
  //   Task* runnableTasks = netpoll_poll(PollStrategy::Blocking);
  //   rxlog("sched poll: netpoll_poll => " << (runnableTasks ? "Task*" : "null"));
  // }

  if (_events.poll(4000) == EvStatus::Error) {
    rxlog("sched poll: events poll error: " << strerror(errno));
    return false;
  }

  // Run tasks in runQ until there are no more running tasks.
  //
  // runQ contains tasks that were previosuly suspended but was
  // eventually resumed because:
  //  a) They are yielding to other tasks, or
  //  b) A condition was met that the task was waiting for.
  // Because of this behavior, rather than all live tasks being
  // in the runQ, we dequeue rather than iterate. This has the
  // upside of supporting _runQ.remove() calls within the loop.
  //
  // Note that if a task enqueues another task in the runq, we
  // run that ask as well.
  //
  Task* T;
  while ((T = _runQ.pop_front()) != nullptr) {
    rxlog("poll: resuming task " << T->ident());

    // There's a chance T gets canceled while running.
    resume(*T, 0);
    // Note: T is invalid at this point. It might have been deallocated.

    // TODO: e.g. ev_run(evloop, EVRUN_NOWAIT);
  }

  // TODO: poll-wait event machine with timeout, if there are wait events.

  // Return true if there's more work to be done
  return _numWaiting;
}


enum ResumeResult : intptr_t {
  ResumeNormal = 0,
  ResumeCancel = INTPTR_MAX-1,
  ResumeKill   = INTPTR_MAX,
};


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
bool Sched::cancel(Task& T) {
  assert(&T._S == this); // only call cancel() with own task
  // Note: We don't set T._cancel just yet since the task
  // might "cancel the cancelation" by catching the exception.
  // Our cancelation deadline will eventually case the task to
  // be killed, unless it ends gracefully first.

  switch (T._status) {
    case TaskStatus::Running: {
      // cancel() was called directly from the task's function.
      throw kCanceled;
      break;
    }
    case TaskStatus::Yielding: {
      // i.e. task is yielding -- remove from runQ so that
      // poll() doesn't attempt to resume the task. We will
      // resume it right now with status ResumeCancel.
      _runQ.remove(&T);
      break;
    }
    case TaskStatus::Waiting: {
      // TODO: Cancel wait condition
      --_numWaiting;
      break;
    }
    default: {
      rxlog("cancel: unexpected task status #" << T._status); abort();
    }
  }
  // resume task, which
  // returns control to suspend(), which
  // checks T._cancel and throws kCanceled, which
  // unless caught by user, returns control to Task::main epilogue.
  return resume(T, ResumeCancel) == TaskStatus::Ended;
}


void Sched::kill(Task& T) {
  T._cancel = Task::Cancelation::Killed;
  resume(T, ResumeKill);
}


// Called by Task::main after T's function has completed,
// but before T has ended.
void Task::removeChildren() {
  rxlog("task " << ident() << " removing children...");
  auto I = _children.begin();
  auto E = _children.end();
  for (; I != E; ++I) {
    Task* cT = *I;
    if (&cT->_S == &_S) {
      // We release our reference to child task.
      // If there are no TaskHandles to the task, this will
      // cause Task::lostAllHandles() to be called which in
      // turn either kills or cancels the task if needed and
      // finally deallocates cT.
      if (!cT->hasOneRef()) {
        // Case: There is at least one user-code TaskHandle holding a
        // reference to cT.

        // Assign root task to T._parent. This is important as the task might
        // have a live TaskHandle which will keep it alive, meaning the task
        // will continue to run (outlive the parent.) So, we need to give it
        // a new parent: this scheduler's root task. If T is suspended,
        // control will be passed to Shed::poll() instead of a dead parent.
        cT->_parent = &_S._rootT;
      }
      cT->releaseRef();
      // Note: cT is invalid at this point (even if cT->hasOneRef() => false).
    } else {
      rxlog("cancelChildren: TODO other scheduler"); abort();
      // cT->_S.async([cT](Sched& S){
      //   S.cancel(*cT);
      // });
    }
  }
  _children.clear(); // DEBUG not necessary

  // TODO: Wait for remaining children-in-other-schedulers to end
  // T._status = TaskStatus::Waiting;
  // T._S.suspend(T, 0);

  rxlog("task " << ident() << " removed children");
}


Cond awaitCond(int fd, Cond c) {
  Task& T = Sched::threadLocal().currentTask();
  //T._S._events.set(fd, c, &T);

  // TODO: handle task cancelation:
  // either catch & rethrow, or put something on the stack that
  // when deallocated removes any event observer.
  return (Cond)T._S.suspend(T, TaskStatus::Waiting);
}


// Jump from T to whatever resume()d it.
// Returns the value passed to next resume() call.
intptr_t Sched::suspend(Task& T, TaskStatus st) {
  // Jump to parent context
  rxlog("suspend: task " << T.ident());
  assert(T._parent != nullptr); // can't suspend root task

  T._status = st;
  auto parentFC = T.parentCtx();
  auto r = (ResumeResult)ctx::jump_fcontext(&T._fc, parentFC, 0, false);
  // will get here when task is resumed
  switch (r) {
    case ResumeCancel: throw kCanceled;
    case ResumeKill:   throw kKilled;
    default:           return r;
  }
}


// Jump from _currT to T, and back again when T is suspended or ends.
// Returns the status of T, useful to check this for TaskStatus::Ended
// as T might be invalid (i.e. have been deallocated.)
TaskStatus Sched::resume(Task& T, intptr_t v) {
  static long _debugN = 0; // XXX DEBUG
  long N = _debugN++;
  rxlog("resume#"<<N<<": task " << T.ident() << " v=" << v);

  assert(_currT != nullptr); // can't resume root task
  auto& currT = *_currT;
  _currT = &T;

  // Switch from currT to T
  intptr_t res;
  if (T._status == TaskStatus::Init) {
    // Start new task
    T._status = TaskStatus::Running;
    T.initCtx(StackAlloc.allocate());
    // Enter Task::main
    res = ctx::jump_fcontext(&currT._fc, T._fc, (intptr_t)&T, false);
  } else {
    // Resume whatever state task is in
    assert(T._status != TaskStatus::Ending);
    assert(T._status != TaskStatus::Ended);
    T._status = TaskStatus::Running;
    res = ctx::jump_fcontext(&currT._fc, T._fc, v, false);
  }

  // Here we _returned_ from resuming, meaning that either
  // a. T was suspended, or
  // b. T ended.

  // Current task is now what was before jumping to T
  _currT = &currT;

  // Check task status
  auto st = T._status;
  switch (st) {

    case TaskStatus::Yielding: {
      rxlog("resume#"<<N<<" return: task " << T.ident() << " yield r="<<res);
      // yield -- add to runQ for immediate execution
      _runQ.push_back(&T);
      break;
    }

    case TaskStatus::Waiting: {
      rxlog("resume#"<<N<<" return: task " << T.ident() << " waiting r="<<res);
      // waiting for some condition
      ++_numWaiting;
      break;
    }

    case TaskStatus::Ended: {
      rxlog("resume#"<<N<<" return: task " << T.ident() << " ended r="<<res);
      taskEnded(T);
      // Note: T is invalid after call to taskEnded
      break;
    }

    default: {
      rxlog("resume#"<<N<<" return: unexpected task status #" << T._status);
      abort();
      break;
    }
  }

  return st;
}


void Sched::taskEnded(Task& T) {
  assert(&T._S == this); // must own T
  assert(T._parent != nullptr); // root task can't end
  auto& parentT = *T._parent;

  // Notify parent that T ended
  if (&parentT._S != &T._S) {
    // Different scheduler -- async
    rxlog("task " << T.ident()
      << " ended -- different S than parent task " << parentT.ident());

    Task* Tptr = &T;
    Task* parentTptr = &parentT;
    parentT._S.async([Tptr, parentTptr](Sched&){
      rxlog("end notify parent task " << Tptr->ident() << " child ended");
      //parentTptr->_children.remove(Tptr);
      Tptr->_S.async([Tptr](Sched& S) {
        // Resume T to where we suspended it: <location A>
        ctx::jump_fcontext(&S._rootT._fc, Tptr->_fc, 0, false);
      });
    });
    // Suspend T until S.poll() dequeues T from _runQ.
    // T is now waiting for parentT._S to remove T from parentT._children.
    auto parentFC = T.parentCtx();
    ctx::jump_fcontext(&T._fc, parentFC, 0, false); // <location A>

    // Release stack_context
    StackAlloc.deallocate(T._sc);

    if (parentT._status != TaskStatus::Ending) {
      T.releaseRef();
    }

  } else {
    // Same scheduler
    rxlog("task " << T.ident()
      << " ended -- same S as parent task " << parentT.ident());
    
    // Release stack_context.
    // This is safe as the task has ended, meaning there are no children
    // and thus T.releaseRef() will not block, but only delete T.
    StackAlloc.deallocate(T._sc);

    if (parentT._status != TaskStatus::Ending) {
      // parentT is still running, meaning that T ended before its parent.
      //
      //    go(T) |————<T>————| ended
      //  --————————<parentT>————————--
      //
      // Note: When a child ends because its parent ends, the parent takes
      // care of removing all children, which is why we check.
      // Remove T from parent's list of children.
      parentT._children.erase(&T);
      T.releaseRef();
    } // else
      // parentT is ending and T closed as part of a cancelation
      // request from parentT.
      //
      //                      cancel(T)
      //                        |
      //    go(T) |————<T>——————|-----| ended
      //  --————————<parentT>———|-------
      //                        |
      //                 end(parentT)
      //
  }
}


void Task::lostAllHandles() {
  // This function is called when the last handle disappears.
  // See documentation in task.h for a description of how this works.
  if (_status != TaskStatus::Ended) {
    rxlog("task " << ident() << " lostAllHandles: cancel ...");
    if (_S.isCurrent()) {
      _S.cancel(*this);
    } else {
      // TODO: schedule cancelation on _S and suspend CurrentSched()._currT
      // on this thread, waiting for cancel to complete, and finally S.async
      // to resume back here.
      rxlog("Task::lostAllHandles: TODO other thread"); abort();
    }
  }
  rxlog("task " << ident() << " lostAllHandles: delete");
  delete this;
}


void Task::main(intptr_t v) {
  assert(v != 0);
  Task& T = *((Task*)v);

  // Start task
  try {
    T._fn();
    rxlog("task " << T.ident() << " exiting: clean");
  } catch (Canceled&) {
    rxlog("task " << T.ident() << " exiting: canceled");
    T._cancel = Task::Cancelation::Canceled;
  } catch (_Killed&) {
    rxlog("task " << T.ident() << " exiting: killed");
    assert(T._cancel == Task::Cancelation::Killed);
  } catch (...) {
    rxlog("task " << T.ident() << " exiting: exception");
    // TODO: Kill T, and perhaps pass the exception to its parent.
    // std::current_exception() -> std::exception_ptr
  }

  T._status = TaskStatus::Ending;

  // Forget, Cancel or Kill any children
  if (!T._children.empty()) {
    T.removeChildren();
  }
  // At this point all children are guaranteed to have ended.

  T._status = TaskStatus::Ended;

  // Jump back to parent context -- back into resume() call.
  auto parentFC = T.parentCtx();
  ctx::jump_fcontext(&T._fc, parentFC, 0, false);
  // Never reached
}


void Sched::end() {
  assert(isCurrent()); // not thread-safe
  auto& T = _rootT;
  if (T._status == TaskStatus::Running) {
    T._status = TaskStatus::Ending;
    if (!T._children.empty()) {
      T.removeChildren();
    }
    T._status = TaskStatus::Ended;
    poll();
  }
}


//  Task that is         |  Task just computes
//  suspended at one     |
//  point:               |
//  ===================  |  ===================
//  go                   |  go
//  -> resume            |  -> resume
//     -> Task::main     |     -> Task::main
//     <- suspend        |  <- /resume
//  <- /resume           |
//  -> resume            |
//     -> /suspend       |
//        Task::main     |
//  <- /resume           |


// Thread-local storage for Sched
#if defined(TLS_ATTR)
  TLS_ATTR static Sched* gTLSS = nullptr;
  #define TLS_GET_SCHED gTLSS
  #define TLS_SET_SCHED(Sptr) gTLSS = (Sptr)

#elif defined(TLS_PTHREAD)
  static pthread_key_t gTLSSKey;
  static struct SchedTLS {
    SchedTLS(){
      #if defined(NDEBUG)
      pthread_key_create(&gTLSSKey, nullptr);
      #else
      assert(pthread_key_create(&gTLSSKey, nullptr) == 0);
      #endif
    }
  } gSchedTLS;
  #define TLS_GET_SCHED (Sched*)pthread_getspecific(gTLSSKey)
  #define TLS_SET_SCHED(Sptr) pthread_setspecific(gTLSSKey, (void*)(Sptr))
#endif

// Scheduler thread function
void Sched::threadMain() {
  // Set this scheduler as the current S for this thread
  TLS_SET_SCHED(this);
  while (1) {
    poll();
  }
  TLS_SET_SCHED(nullptr);
}

Sched& Sched::threadLocal() {
  Sched* s = TLS_GET_SCHED;
  if (s == nullptr) {
    // Initialize scheduler for main thread.
    // Note that s==null only happens for the main thread as we explicitly
    // do TLS_SET_SCHED when starting new scheduler threads.
    //
    // gSchedsSize = size_t(std::thread::hardware_concurrency());
    // rxlog("initializing " << numSchedulers << " scheds");
    gScheds = new Sched[gSchedsSize];
    s = &gScheds[0];
    s->_running = 1; // main thread already exists
    s->_threadID = std::this_thread::get_id();
    TLS_SET_SCHED(s);
  }
  return *s;
}


// Process finalizer takes care of ending the main scheduler's root task
struct ProcessFin {
  ~ProcessFin() {
    if (gScheds != nullptr) {
      rxlog("~ProcessFin");
      Sched& S = gScheds[0];
      S.end();
      // TODO: End all schedulers?
    }
  }
} gProcessFin;


TaskHandle go(TaskFn&& fn) {
  // Get scheduler for current thread
  auto& currS = Sched::threadLocal();

  // Calling task
  auto& parentT = currS.currentTask();

  // Allocate a new task ID
  auto tid = NextTaskID();

  // Select the most appropriate scheduler
  Sched& S = gScheds[size_t(tid) % gSchedsSize];

  // Allocate and partially initialize new task
  auto T = new Task(S, tid, &parentT, std::move(const_cast<TaskFn&&>(fn)));
  TaskHandle handle{T};

  // Register T as child of parentT
  parentT._children.emplace(T);
  T->retainRef();

  if (&currS == &S) {
    // Schedule on same thread
    rxlog("go: scheduling task " << tid
      << " on same thread as task " << parentT.ident());
    S.resume(*T, (intptr_t)T);

  } else {
    // Schedule on other thread
    rxlog("go: scheduling task " << tid
      << " on other thread than task " << parentT.ident());
    S.async([T, fn=std::move(fn)](Sched& S){
      assert(S._currT == &S._rootT); // we make this assumption in Task::main
      // TODO: Figure out if it's better to add the task to the scheduler's
      // _runQ instead of immediately resuming the task.
      S.resume(*T, (intptr_t)T);
    });
  }

  return handle;
}


void yield() {
  auto& S = Sched::threadLocal();
  S.suspend(S.currentTask(), TaskStatus::Yielding);
}


void tsleep(uint64_t ns) {
  auto& S = Sched::threadLocal();
  // TODO
  // S.suspend(*S._currT, 0);
}


bool SchedPoll() {
  return Sched::threadLocal().poll();
}
