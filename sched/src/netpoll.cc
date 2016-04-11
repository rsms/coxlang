#include "netpoll.h"
#include "atomic.h"
#include "os.h"
#include "log.h"
#include "t.h"
#include "common.h"
#include <assert.h>

template <typename T, size_t N>
constexpr size_t countof(T const (&)[N]) noexcept { return N; }

using ScopeLock = std::lock_guard<std::mutex>;

static T* pdReady = (T*)intptr_t(1);
static T* pdWait  = (T*)intptr_t(2);

static PollCache pollCache;
static atomic_t uintptr_t netpollActive = 0;

// Implementation-specific functions
static bool netpoll_imp_init();
static bool netpoll_imp_open(intptr_t fd, PollDesc&);
static bool netpoll_imp_close(intptr_t fd);
static void netpoll_imp_arm(PollDesc*, int);
T* netpoll_poll(PollStrategy);

void netpoll_init() {
  static sync_once_flag once;
  sync_once(&once, {
    if (!netpoll_imp_init()) {
      fprintf(stderr, "netpoll_init failed: %s (%d)\n", strerror(errno), errno);
      abort();
    }
  });
  AtomicStore(&netpollActive, 1);
}

bool netpoll_active() {
  return AtomicLoad(&netpollActive) != 0;
}

PollDesc* netpoll_open(intptr_t fd) {
  if (netpollActive == 0) { netpoll_init(); }
  PollDesc& pd = pollCache.alloc();

  assert(pd.wt == nullptr || pd.wt == pdReady
    ||!"blocked write on free descriptor");
  assert(pd.rt == nullptr || pd.rt == pdReady
    ||!"blocked read on free descriptor");
  
  pd.fd = fd;
  pd.closing = false;
  pd.seq++;
  pd.rt = nullptr;
  pd.rz = -1;
  pd.rd = 0;
  pd.wt = nullptr;
  pd.wz = -1;
  pd.wd = 0;

  if (!netpoll_imp_open(fd, pd)) {
    int e = errno;
    pollCache.free(pd);
    errno = e;
    return nullptr;
  }

  return &pd;
}

void netpoll_close(PollDesc& pd) {
  assert(pd.closing ||!"close w/o unblock");
  assert(pd.wt == nullptr || pd.wt == pdReady
    ||!"blocked write on closing descriptor");
  assert(pd.rt == nullptr || pd.rt == pdReady
    ||!"blocked read on closing descriptor");
  netpoll_imp_close(pd.fd); // TODO: Check for success
  pollCache.free(pd);
}

inline static int netpoll_checkerr(PollDesc& pd, int mode) {
  if (pd.closing) {
    errno = EBADF;
    return 1; // errClosing
  }
  if ((mode == 'r' && pd.rd < 0) || (mode == 'w' && pd.wd < 0)) {
    errno = ETIMEDOUT;
    return 2; // errTimeout
  }
  return 0;
}

bool netpoll_await_commit(T& t, intptr_t v) {
  rxlog("netpoll_await_commit");
  void* p = (void*)v;
  auto tpp = (atomic_t(T*)*)p;
  // if *tpp == pdWait, set *tpp to t and return true to signal that
  // we are waiting, otherwise *tpp is pdReady and we return false
  // to have t be resumed right away.
  T* expect = pdWait;
  return AtomicCasRelAcq(tpp, &expect, &t);
}

// returns true if IO is ready, or false if timedout or closed.
// • strategy - when Blocking: wait only for completed IO and ignore errors.
bool netpoll_await(PollDesc& pd, int mode, PollStrategy strategy) {
  assert(mode == 'w' || mode == 'r' /* no support for 'r'+'w' */);

  int err = netpoll_checkerr(pd, mode);
  if (err != 0) {
    return false;
  }

  auto tpp = (atomic_t(T*)*)((mode == 'w') ? &pd.wt : &pd.rt);

  // set the tpp semaphore to "wait"
  T* old = *tpp;
  while (1) {
    if (old == pdReady) {
      // pd is readable or writable (whatever mode asked for)
      return true;
    }
    if (old != nullptr) {
      panic("double wait");
    }
    // Mark pd as "waiting for <mode>"
    if (AtomicCasRelAcq(tpp, &old, pdWait)) {
      // We won the race and set pd to wait
      break;
    }
    // We lost the race. Check again for "ready".
    // Note that AtomicCasRelAcq stores actual value of tpp
    // into old on failure, so no need to reload old from tpp.
  }

  // Need to recheck error states after setting tpp to WAIT.
  // This is necessary because
  // runtime_pollUnblock/runtime_pollSetDeadline/deadlineimpl
  // do the opposite: store to closing/rd/wd, membarrier, load of rg/wg.
  if (strategy == PollBlocking || netpoll_checkerr(pd, mode) == 0) {
    rxlog("netpoll_await: parking task");
    t_park(netpoll_await_commit, (intptr_t)tpp, "IO wait");
  }

  // be careful to not lose concurrent READY notification
  old = AtomicXchg(tpp, nullptr, AtomicConsume);
  if (old != nullptr && old != pdReady && old != pdWait) {
    panic("corrupted state");
  }

  if (old != pdReady) {
    netpoll_checkerr(pd, mode);
    return false; // check errno
  }
  return true;
}


// returns a task waiting for mode, or nullptr if nothing is waiting.
static T* netpoll_unblock(PollDesc& pd, int mode, bool ioready) {
  assert(mode == 'w' || mode == 'r' /* no support for 'r'+'w' */);
  
  auto tpp = (atomic_t(T*)*)((mode == 'w') ? &pd.wt : &pd.rt);

  rxlog("netpoll_unblock(" << (char)mode << "): pd.rt="
    << pd.rt << ", pd.wt=" << pd.wt);

  while (1) {
    T* oldT = *tpp;
    if (oldT == pdReady) {
      return nullptr;
    }
    if (oldT == nullptr && !ioready) {
      // Only set READY for ioready. runtime_pollWait
      // will check for timeout/cancel before waiting.
      return nullptr;
    }
    // Set pd.<mode>T to "ready"
    T* newT = ioready ? pdReady : nullptr;
    if (AtomicCasRelAcq(tpp, &oldT, newT)) {
      if (oldT == pdReady || oldT == pdWait) {
        // Was cleared by another thread during CAS
        oldT = nullptr;
      }
      return oldT;
    }
  }
  // unreachable
}

// Make pd ready.
// Newly runnable tasks (if any) are chained onto tp.
// New value of tp is returned.
static T* netpoll_ready(T* tp, PollDesc& pd, int mode) {
  rxlog("netpoll_ready: mode="
    << (mode == 'r' ? "r" : mode == 'w' ? "w" : "r+W"));

  T* rT = nullptr;
  T* wT = nullptr;
  if (mode == 'r' || mode == 'r'+'w') {
    rT = netpoll_unblock(pd, 'r', /*ioready=*/true);
    rxlog("netpoll_ready: netpoll_unblock(r) => " << rT);
  }
  if (mode == 'w' || mode == 'r'+'w') {
    wT = netpoll_unblock(pd, 'w', /*ioready=*/true);
    rxlog("netpoll_ready: netpoll_unblock(w) => " << wT);
  }
  // Link tasks onto chain at tp
  if (rT != nullptr) {
    rT->schedlink = tp;
    tp = rT;
  }
  if (wT != nullptr) {
    wT->schedlink = tp;
    tp = wT;
  }
  return tp;
}

static void netpoll_unblockclose(PollDesc& pd) {
  atomic_t(T*) rt = nullptr;
  atomic_t(T*) wt = nullptr;
  { ScopeLock lock(pd.lock);
    if (pd.closing) {
      throw "already closing";
    }
    pd.closing = true;
    ++pd.seq;
    // full memory barrier between store to closing
    // and read of rg/wg in netpoll_unblock:
    AtomicStoreX(&rt, nullptr, AtomicSeqCst);
    rt = netpoll_unblock(pd, 'r', /*ioready=*/false);
    wt = netpoll_unblock(pd, 'w', /*ioready=*/false);
    // if (pd.rtimer.f != nil) {
    //   deltimer(&pd.rtimer);
    //   pd.rtimer.f = nil;
    // }
    // if pd.wtimer.f != nil {
    //   deltimer(&pd.wtimer);
    //   pd.wtimer.f = nil
    // }
  }
  if (rt) {
    t_ready(*rt);
  }
  if (wt) {
    t_ready(*wt);
  }
}

// evicts fd from the pending list, unblocking any I/O running on this PollDesc.
void PollDesc::evict() {
  netpoll_unblockclose(*this);
}

// —————————————————————————————————————————————————————————————————————————————
// PollCache

PollDesc& PollCache::alloc() {
  constexpr size_t BlockSize = 4096;
  static_assert(BlockSize / sizeof(PollDesc) > 0, "BlockSize too small");

  PollDesc* pd = _freelist.tryGet();

  if (pd == nullptr) {
    // Allocate a slab of PollDesc
    size_t n = BlockSize / sizeof(PollDesc);
    pd = (PollDesc*)calloc(n, sizeof(PollDesc));
    // PollDesc uses std::mutex which needs explicit initialization
    for (size_t i = 0; i != n; i++) {
      std::allocator<std::mutex>().construct(&pd->lock);
    }
    // Put any extra-allocated PollDescs into the freelist
    if (n > 1) {
      _freelist.putn(&pd[1], n-1);
    }
  }

  return *pd;
}

void PollCache::free(PollDesc& pd) {
  _freelist.put(&pd);
}

// —————————————————————————————————————————————————————————————————————————————
// kqueue

static int kq = -1;

static bool netpoll_imp_init() {
  kq = kqueue();
  if (kq == -1) {
    return false;
  }
  closeonexec(kq);
  return true;
}

static bool netpoll_imp_open(intptr_t fd, PollDesc& pd) {
  // Arm both EVFILT_READ and EVFILT_WRITE in edge-triggered mode (EV_CLEAR)
  // for the whole fd lifetime.
  // The notifications are automatically unregistered when fd is closed.
  struct kevent ev[2];
  ev[0].ident  = fd;
  ev[0].filter = EVFILT_READ;
  ev[0].flags  = EV_ADD | EV_CLEAR;
  ev[0].fflags = 0;
  ev[0].data   = 0;
  ev[0].udata  = (void*)&pd;
  ev[1] = ev[0];
  ev[1].filter = EVFILT_WRITE;
  return kevent(kq, &ev[0], 2, nullptr, 0, nullptr) == 0;
}

static bool netpoll_imp_close(intptr_t) {
  // Don't need to unregister because calling close()
  // on fd will remove any kevents that reference the descriptor.
  return true;
}

static void netpoll_imp_arm(PollDesc*, int) {
  // not used for kqueue
}

// Polls for ready network connections.
// Returns list of tasks that become runnable.
T* netpoll_poll(PollStrategy strategy) {
  if (kq == -1) { return nullptr; }
  struct timespec* tsp = nullptr;
  struct timespec ts{0,0};
  if (strategy == PollImmediate) {
    tsp = &ts;
  }
  struct kevent events[64];
  int n;
retry:
  n = kevent(kq, nullptr, 0, &events[0], countof(events), tsp);
  if (n == -1) {
    if (errno != EINTR) {
      fprintf(stderr, "netpoll: kevent on fd %d failed with [errno %d] %s\n",
        kq, n, strerror(errno));
      abort(); // FIXME
    }
    goto retry;
  }
  T* tp = nullptr; // list of ready tasks
  for (int i = 0; i < n; i++) {
    // Note: Events arrive separately for read and write availability.
    // We might want to consider merging events to the same PollDesc to
    // allow task resume optimization.
    auto& ev = events[i];
    int mode = 0;
    auto pd = (PollDesc*)ev.udata;
    switch (ev.filter) {
      case EVFILT_READ: {
        assert(pd != nullptr);
        mode = 'r';
        // Note: We could check ev.flags for EV_EOF.
        // ev.data contains the number of bytes available to read.
        pd->rz = ev.data;
        rxlog("netpoll_poll: got READ event for fd " << ev.ident);
        break;
      }
      case EVFILT_WRITE: {
        assert(pd != nullptr);
        mode = 'w';
        // ev.data will contain the amount of space remaining in the write buffer.
        pd->wz = ev.data;
        rxlog("netpoll_poll: got WRITE event for fd " << ev.ident);
        break;
      }
      default: {
        break;
      }
    }
    if (mode != 0) {
      // Sets 0, 1 or 2 tasks (as links) at tp
      tp = netpoll_ready(tp, *pd, mode);
    }
  }
  if (strategy == PollBlocking && tp == nullptr) {
    goto retry;
  }
  return tp;
}

