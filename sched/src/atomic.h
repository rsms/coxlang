#pragma once
#include "target.h"

#if !defined(__STDC_NO_ATOMICS__) && \
    ((defined(__has_feature) && __has_feature(c_atomic)) \
    || (defined(__has_extension) && __has_extension(c_atomic)))
  // Declaring something atomic_t(T) generally means:
  // - Load and store instructions are not reordered (similar to "volatile").
  // - Instead of a MOV, an XCHG instruction is used for stores.
  // It does _not_ imply thread memory synchronization.
  // For thread synchronization, you need to use the Atomic* functions below.
  #define atomic_t _Atomic
  // Note: std::atomic goes bananas if we include stdatomic.h

  typedef enum AtomicOrder {
    AtomicRelaxed = __ATOMIC_RELAXED,
      // Relaxed operation: there are no synchronization or ordering constraints,
      // only atomicity is required of this operation.

    AtomicConsume = __ATOMIC_CONSUME,
      // A load operation with this memory order performs a consume operation on
      // the affected memory location: no reads in the current thread dependent
      // on the value currently loaded can be reordered before this load.
      // This ensures that writes to data-dependent variables in other threads
      // that release the same atomic variable are visible in the current thread.
      // On most platforms, this affects compiler optimizations only.

    AtomicAcquire = __ATOMIC_ACQUIRE,
      // A load operation with this memory order performs the acquire operation
      // on the affected memory location: no memory accesses in the current
      // thread can be reordered before this load. This ensures that all writes
      // in other threads that release the same atomic variable are visible in
      // the current thread.

    AtomicRelease = __ATOMIC_RELEASE,
      // A store operation with this memory order performs the release operation:
      // no memory accesses in the current thread can be reordered after this
      // store. This ensures that all writes in the current thread are visible in
      // other threads that acquire the same atomic variable and writes that
      // carry a dependency into the atomic variable become visible in other
      // threads that consume the same atomic.

    AtomicAcqRel  = __ATOMIC_ACQ_REL,
      // A read-modify-write operation with this memory order is both an acquire
      // operation and a release operation. No memory accesses in the current
      // thread can be reordered before this load, and no memory accesses in the
      // current thread can be reordered after this store. It is ensured that
      // all writes in other threads that release the same atomic variable are
      // visible before the modification and the modification is visible in other
      // threads that acquire the same atomic variable.

    AtomicSeqCst  = __ATOMIC_SEQ_CST
      // Any operation with this memory order is both an acquire operation and a
      // release operation, plus a single total order exists in which all threads
      // observe all modifications (see below) in the same order.

  } AtomicOrder;

  // atomic_t(T) v;
  // void AtomicStore(volatile A* p, T desired);
  //    T AtomicLoad(volatile A* p)
  #define AtomicStore(p, v) __c11_atomic_store(p, v, AtomicSeqCst)
  #define AtomicStoreX(p, v, mo) __c11_atomic_store(p, v, mo)
  #define AtomicLoad(p)     __c11_atomic_load(p, AtomicSeqCst)

  #define AtomicFetchAdd(p, d, memorder) __c11_atomic_fetch_add(p, d, memorder)
  #define AtomicFetchSub(p, d, memorder) __c11_atomic_fetch_sub(p, d, memorder)

  // AtomicXAdd(volatile A*, T delta) -- i.e. atomically: *A += D; return *A;
  #define AtomicXAdd(A, D) (__c11_atomic_fetch_add(A, D, AtomicRelaxed) + (D))

  #define AtomicFence(memorder) __c11_atomic_thread_fence(memorder)

  // C AtomicXchg(volatile A* p, C desired, MemOrder);
  #define AtomicXchg(p, v, memorder) __c11_atomic_exchange(p, v, memorder)

  #define HAS_STDC_ATOMIC
#else
  #warning "No atomics support -- atomic_{store,load} fallback to unordered"
  #define atomic_t
  #define AtomicStore(p, v) ((*p) = (v))
  #define AtomicLoad(p)     (*p)
#endif

// sync_once:
//   static sync_once_flag f;
//   sync_once(&f, { /* only run once */ });
//   // stores and loads inside once block are synced
#define sync_once_flag volatile long
#define sync_once(f, block) \
  ({ \
    (*(f) != 2) && _sync_once(f) && ({ block; __sync_synchronize(); 0; }); \
  })
int _sync_once(sync_once_flag*);

// bool AtomicCasWeak(volatile A* p,
//                    C* expected, C desired,
//                    memory_order succ,  memory_order fail)
//
// bool AtomicCasStrong(volatile A* p,
//                    C* expected, C desired,
//                    memory_order succ,  memory_order fail)
//
// Atomically compares the value pointed to by obj with the value pointed
// to by expected, and if those are equal, replaces the former with desired
// (performs read-modify-write operation). Otherwise, loads the actual value
// pointed to by obj into *expected (performs load operation).
//
// The weak forms of the function is allowed to fail spuriously, that is,
// act as if *this != expected even if they are equal.
// When a compare-and-exchange is in a loop, the weak version will yield
// better performance on some platforms.
//
// When a weak compare-and-exchange would require a loop and a strong one
// would not, the strong one is preferable unless the object representation
// of T may include padding bits, trap bits, or offers multiple object
// representations for the same value (e.g. floating-point NaN). In those
// cases, weak compare-and-exchange typically works because it quickly
// converges on some stable object representation.
//
#define AtomicCasStrong  __c11_atomic_compare_exchange_strong
#define AtomicCasWeak    __c11_atomic_compare_exchange_weak

// Shorthand for: Success release-to-consumers; Fail: load-acquire
#define AtomicCasRelAcq(p, e, d) \
  AtomicCasWeak((p), (e), (d), AtomicRelease, AtomicAcquire)

// bool AtomicCas(T* p, T expectedVal, T newVal)
#define AtomicCas(p, ov, nv) __sync_bool_compare_and_swap((p), (ov), (nv))

// int64_t AtomicXchg64(volatile int64_t* p, int64_t v)
// int32_t AtomicXchg32(volatile int32_t* p, int32_t v)
// void*   AtomicXchgPtr(volatile void* p, void* p)
#if RX_TARGET_ARCH_X64 || RX_TARGET_ARCH_X86
static inline int64_t AtomicXchg64(volatile int64_t* p, int64_t v) {
  __asm__ __volatile__("xchgq %0,%1"
        :"=r" (v)
        :"m" (*p), "0" (v)
        :"memory");
  return v;
}
static inline int32_t AtomicXchg32(volatile int32_t* p, int32_t v) {
  __asm__ __volatile__("xchgl %0,%1"
        :"=r" (v)
        :"m" (*p), "0" (v)
        :"memory");
  return v;
}
static inline void* AtomicXchgPtr(volatile void** p, void* v) {
  #if __LP64__
  return (void*)AtomicXchg64((volatile int64_t*)p, (int64_t)v);
  #else
  return (void*)AtomicXchg32((volatile int32_t*)p, (int32_t)v);
  #endif
}
#elif defined(__clang__)
  #define AtomicXchg32 __sync_swap
  #define AtomicXchg64 __sync_swap
#else
  #define AtomicXchg32(p, v) ({
    decltype(v) ov = *ptr;
    do {} while (!__sync_bool_compare_and_swap(p, ov, v));
    ov;
  })
  #define AtomicXchg64(p, v) AtomicXchg32(p, v)
#endif


#define RX_ATOMIC_ENABLE_LEGACY
#ifdef RX_ATOMIC_ENABLE_LEGACY
/*

T rx_atomic_swap(T *ptr, T value)
  Atomically swap integers or pointers in memory. Note that this is more than
  just CAS.
  E.g: int old_value = rx_atomic_swap(&value, new_value);

void rx_atomic_add32(i32* operand, i32 delta)
  Increment a 32-bit integer `operand` by `delta`. There's no return value.

T rx_atomic_add_fetch(T* operand, T delta)
  Add `delta` to `operand` and return the resulting value of `operand`

T rx_atomic_sub_fetch(T* operand, T delta)
  Subtract `delta` from `operand` and return the resulting value of `operand`

bool rx_atomic_cas_bool(T* ptr, T oldval, T newval)
  If the current value of *ptr is oldval, then write newval into *ptr.
  Returns true if the operation was successful and newval was written.

T rx_atomic_cas(T* ptr, T oldval, T newval)
  If the current value of *ptr is oldval, then write newval into *ptr.
  Returns the contents of *ptr before the operation.

-----------------------------------------------------------------------------*/

#define RX_HAS_SYNC_BUILTINS \
  (defined(__clang__) || (defined(__GNUC__) && (__GNUC__ >= 4)))
#if !RX_HAS_SYNC_BUILTINS
  #warning "Unsupported compiler: Missing support for atomic operations"
#endif

// T rx_atomic_swap(T *ptr, T value)
#if defined(__clang__)
  // This is more efficient than the below fallback
  #define rx_atomic_swap __sync_swap
#elif RX_HAS_SYNC_BUILTINS
  static inline void* __attribute__((unused))
  _rx_atomic_swap(void* volatile* ptr, void* value) {
    void* oldval;
    do {
      oldval = *ptr;
    } while (__sync_val_compare_and_swap(ptr, oldval, value) != oldval);
    return oldval;
  }
  #define rx_atomic_swap(ptr, value) \
    _rx_atomic_swap((void* volatile*)(ptr), (void*)(value))
#endif

// void rx_atomic_add32(T* operand, T delta)
#if RX_TARGET_ARCH_X64 || RX_TARGET_ARCH_X86
  inline static void __attribute__((unused))
  rx_atomic_add32(int* operand, int delta) {
    // From http://www.memoryhole.net/kyle/2007/05/atomic_incrementing.html
    __asm__ __volatile__ (
      "lock xaddl %1, %0\n" // add delta to operand
      : // no output
      : "m" (*operand), "r" (delta)
    );
  }
  #ifdef __cplusplus
  inline static void __attribute__((unused))
  rx_atomic_add32(unsigned int* o, unsigned int d) {
    rx_atomic_add32((int*)o, static_cast<int>(d)); }
  inline static void __attribute__((unused))
  rx_atomic_add32(volatile int* o, volatile int d) {
    rx_atomic_add32((int*)o, static_cast<int>(d)); }
  inline static void __attribute__((unused))
  rx_atomic_add32(volatile unsigned int* o, volatile unsigned int d) {
    rx_atomic_add32((int*)o, static_cast<int>(d)); }
  #endif
#elif RX_HAS_SYNC_BUILTINS
  #define rx_atomic_add32 __sync_add_and_fetch
#else
  #error "Unsupported compiler: Missing support for atomic operations"
#endif

// T rx_atomic_sub_fetch(T* operand, T delta)
#if RX_HAS_SYNC_BUILTINS
  #define rx_atomic_sub_fetch __sync_sub_and_fetch
#endif

// T rx_atomic_add_fetch(T* operand, T delta)
#if RX_HAS_SYNC_BUILTINS
  #define rx_atomic_add_fetch __sync_add_and_fetch
#endif


// bool rx_atomic_cas_bool(T* ptr, T oldval, T newval)
#if RX_HAS_SYNC_BUILTINS
  #define rx_atomic_cas_bool(ptr, oldval, newval) \
    __sync_bool_compare_and_swap((ptr), (oldval), (newval))
#endif


// T rx_atomic_cas(T* ptr, T oldval, T newval)
#if RX_HAS_SYNC_BUILTINS
  #define rx_atomic_cas(ptr, oldval, newval) \
    __sync_val_compare_and_swap((ptr), (oldval), (newval))
#endif


// void rx_atomic_barrier()
#if RX_HAS_SYNC_BUILTINS
  #define rx_atomic_barrier() __sync_synchronize()
#endif

typedef volatile long rx_once_t;
#define RX_ONCE_INIT 0L
inline static bool __attribute__((unused)) rx_once(rx_once_t* token) {
  return *token == 0L && rx_atomic_cas_bool(token, 0L, 1L);
}


// Spinlock
#ifdef __cplusplus
namespace rx {

struct Spinlock {
  void lock() noexcept { while (!try_lock()); }
  bool try_lock() noexcept { return rx_atomic_cas_bool(&_v, 0L, 1L); }
  void unlock() noexcept { _v = 0L; }
private:
  volatile long _v = 0L;
};

struct ScopedSpinlock {
  ScopedSpinlock(Spinlock& lock) : _lock{lock} { _lock.lock(); }
  ~ScopedSpinlock() { _lock.unlock(); }
private:
  Spinlock& _lock;
};

} // namespace
#endif // __cplusplus

#endif /* RX_ATOMIC_ENABLE_LEGACY */
