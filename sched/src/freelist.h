#pragma once
#include "atomic.h"
#include "assert.h"

#if ATOMIC_POINTER_LOCK_FREE == 2 /* ptr CAS is always lock-free */

// If you know your target architecture supports a double-wide compare-and-swap
// instruction, you can avoid the reference counting dance and use the naïve
// algorithm with a tag to avoid ABA, while still having it be lock-free.
// (If the tag of the head pointer is incremented each time it's changed,
// then ABA can't happen because all head values appear unique even if an
// element is re-added.)

#define FreeListEntryFields(T) \
  friend struct FreeList<T>; \
  std::atomic<T*> _freelist_next;

template<typename T>
struct FreeList {
  FreeList() : _head(HeadPtr()) {}

  // Puts multiple entries into the freelist
  void putn(T* p, size_t n) {
    assert(n > 0);
    // State: head=A, A.next=B, B.next=C, C.next=null
    // putn(p=[X, Y, Z], n=3)
    //   e = p = X      // e = X, i = 1
    //     X->next = Y  // e = Y, i = 2
    //     Y->next = Z  // e = Z, i = 3
    //   ch = load(head)
    //   nh = p = X
    //   loop:
    //     store(X.next, ch.p)
    //     if CAS(ch, nh):
    //       break
    // State: head=X, X.next=Y, Y.next=Z, Z.next=A, A.next=B, B.next=C, C.next=null
    T* e = p;
    for (size_t i = 1; i < n; ++i) {
      e = e->_freelist_next = &e[i];
    }
    HeadPtr ch = _head.load(std::memory_order_relaxed);
    HeadPtr nh = { p }; // {ptr=X}
    do {
      nh.tag = ch.tag + 1;
      // Attempt to store Z.next=A
      e->_freelist_next.store(ch.ptr, std::memory_order_relaxed);
    } while (!_head.compare_exchange_weak(ch, nh,
                std::memory_order_release, std::memory_order_relaxed));
  }

  // Put a single entry into the freelist
  void put(T* e) {
    // State: head=A, A.next=B, B.next=C, C.next=null
    // put(X)
    //   ch = load(head)
    //   nh = X
    //   loop:
    //     store(X.next, ch.p)
    //     if CAS(ch, nh):
    //       break
    // State: head=X, X.next=A, A.next=B, B.next=C, C.next=null
    HeadPtr ch = _head.load(std::memory_order_relaxed);
    HeadPtr nh = { e };
    do {
      nh.tag = ch.tag + 1;
      e->_freelist_next.store(ch.ptr, std::memory_order_relaxed);
    } while (!_head.compare_exchange_weak(ch, nh,
                std::memory_order_release, std::memory_order_relaxed));
  }

  T* tryGet() {
    // State: head=A, A.next=B, B.next=C, C.next=nullptr
    //   ch = load(head)  // ch.p == A
    //   while ch.p != null:
    //     nh.p = load(ch.p->next) // == B, i.e. ch.p == A, ch.p->next == A.next
    //     if CAS(head, nh):
    //       break
    //   return ch.p // == A
    // State: head=B, B.next=C, C.next=nullptr
    //
    HeadPtr ch = _head.load(std::memory_order_acquire);
    HeadPtr nh;
    while (ch.ptr != nullptr) {
      nh.ptr = ch.ptr->_freelist_next.load(std::memory_order_relaxed);
      nh.tag = ch.tag + 1;
      if (_head.compare_exchange_weak(ch, nh,
            std::memory_order_release, std::memory_order_acquire))
      {
        break;
      }
    }
    return ch.ptr;
  }

  // Useful for traversing the list when there's no contention
  // (e.g. to destroy remaining nodes)
  T* headUnsafe() const {
    return _head.load(std::memory_order_relaxed).ptr;
  }

private:
  struct HeadPtr {
    T* ptr;
    uintptr_t tag;
  };
  std::atomic<HeadPtr> _head;
};


#else /* if ATOMIC_POINTER_LOCK_FREE == 2 -- ptr CAS not always lock-free */


#define FreeListEntryFields(T) \
  friend struct FreeList<T>; \
  std::atomic<std::uint32_t> _freelist_refs; \
  std::atomic<T*>            _freelist_next;


// A simple CAS-based lock-free free list. Not the fastest thing in the world
// under heavy contention, but simple and correct (assuming nodes are never
// freed until after the free list is destroyed), and fairly speedy under low
// contention.
template<typename T>
struct FreeList {
  FreeList() : _head(nullptr) { }

  void put(T* e) {
    // We know that the should-be-on-freelist bit is 0 at this point,
    // so it's safe to set it using a fetch_add.
    if (e->_freelist_refs.fetch_add(ShouldBeOnFreelist,
        std::memory_order_release) == 0)
    {
      // Oh look! We were the last ones referencing this entry, and we know
      // we want to add it to the free list, so let's do it!
      _put(e);
    }
  }

  T* tryGet() {
    auto head = _head.load(std::memory_order_acquire);
    while (head != nullptr) {
      auto prevHead = head;
      auto refs = head->_freelist_refs.load(std::memory_order_relaxed);
      if ((refs & REFS_MASK) == 0 ||
        !head->_freelist_refs.compare_exchange_strong(
          refs,
          refs + 1,
          std::memory_order_acquire,
          std::memory_order_relaxed
        ))
      {
        head = _head.load(std::memory_order_acquire);
        continue;
      }

      // Good, reference count has been incremented (it wasn't at zero),
      // which means we can read the next and not worry about it changing
      // between now and the time we do the CAS
      auto next = head->_freelist_next.load(std::memory_order_relaxed);
      if (_head.compare_exchange_strong(head, next,
          std::memory_order_acquire, std::memory_order_relaxed)) {
        // Yay, got the node. This means it was on the list, which means
        // shouldBeOnFreeList must be false no matter the refcount (because
        // nobody else knows it's been taken off yet, it can't have been
        // put back on).
        assert((head->_freelist_refs.load(std::memory_order_relaxed) &
          ShouldBeOnFreelist) == 0);

        // Decrease refcount twice, once for our ref, and once for the
        // list's ref
        head->_freelist_refs.fetch_add(-2, std::memory_order_relaxed);

        return head;
      }

      // OK, the head must have changed on us, but we still need to
      // decrease the refcount we increased
      refs = prevHead->_freelist_refs.fetch_add(-1, std::memory_order_acq_rel);
      if (refs == ShouldBeOnFreelist + 1) {
        _put(prevHead);
      }
    }

    return nullptr;
  }

  // Useful for traversing the list when there's no contention
  // (e.g. to destroy remaining nodes)
  T* headUnsafe() const {
    return _head.load(std::memory_order_relaxed);
  }

private:
  void _put(T* e) {
    // State: head=A, A.next=B, B.next=C, C.next=null
    // _put(X)
    //   ch = load(head) // A
    //   loop:
    //     store(X.next, ch)
    //     store(X.refs, 1)
    //     if CAS(head, ch):
    //       break
    //     if add(e.refs, ShouldBeOnFreelist - 1) != 1
    //       break // WTF?!
    // State: head=X, X.next=A, A.next=B, B.next=C, C.next=null

    // Since the refcount is zero, and nobody can increase it once it's
    // zero (except us, and we run only one copy of this method per node
    // at a time, i.e. the single thread case), then we know we can safely
    // change the next pointer of the node; however, once the refcount is
    // back above zero, then other threads could increase it (happens under
    // heavy contention, when the refcount goes to zero in between a load
    // and a refcount increment of a node in try_get, then back up to
    // something non-zero, then the refcount increment is done by the other
    // thread) -- so, if the CAS to add the node to the actual list fails,
    // decrease the refcount and leave the add operation to the next thread
    // who puts the refcount back at zero (which could be us, hence the loop).
    auto ch = _head.load(std::memory_order_relaxed);
    while (1) {
      e->_freelist_next.store(ch, std::memory_order_relaxed);
      e->_freelist_refs.store(1, std::memory_order_release);
      if (!_head.compare_exchange_strong(ch, e,
          std::memory_order_release, std::memory_order_relaxed)) {
        // Hmm, the add failed, but we can only try again when the
        // refcount goes back to zero
        if (e->_freelist_refs.fetch_add(ShouldBeOnFreelist - 1,
            std::memory_order_release) == 1) {
          continue;
        }
      }
      return;
    }
  }

  static const std::uint32_t REFS_MASK = 0x7FFFFFFF;
  static const std::uint32_t ShouldBeOnFreelist = 0x80000000;

  std::atomic<T*> _head;
};

#endif