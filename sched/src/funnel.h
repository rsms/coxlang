#pragma once
#include "atomic.h"

namespace rx {

// Multiple Producer, Single Consumer lock-free (well, one CAS per "enqueue") queue.
//
// We call it a funnel since input order is indeterministic due to the possiblity of thread race
// conditions. In the case of a race condition on `push(T*)`, one thread always makes progress.
// However which of the threads that makes progress (i.e. places an entry in the queue) first
// depends on variables outside of our control, like preemptive scheduling, thus making insertion
// order indeterministic.
//
// You need to define an entry type which needs a `_next_link` member. There are two ways of
// defining an entry type:
//
//  struct myentry;
//  struct myentry : rx::Funnel<myentry>::entry {
//    int foo = 1;
//  };
//
// Or:
//
//  struct myentry {
//    myentry* volatile _next_link;
//    int foo = 1;
//  };
//

template <typename T>
struct FunnelEntry {
  // Entry type `T` must have the following `_next_link` member that's nullptr by default
  T* volatile _next_link = nullptr;
};

template <typename T>
struct Funnel {
  // An empty funnel
  Funnel();

  // Allow moving
  Funnel(Funnel&&) = default;
  Funnel& operator=(Funnel&&) = default;

  // Put an entry into the funnel. Thread-safe. Returns true if the queue was empty.
  bool push(T*);

  // Dequeue the next entry waiting in the funnel. Returns nullptr if the funnel is empty.
  // Should always be called from the same thread for a specific funnel.
  T* pop();

  bool empty() { return _head == (T*)&_sentinel; }

  // Apply function to each element
  template <typename F> void foreach(F) const;

// ------------------------------------------------------------------------------------------------
private:
  struct Sentinel { T* volatile _next_link = nullptr; };
  T* volatile   _head;
  unsigned char _pad = 0;     // cache line hack
  T*            _tail;
  Sentinel      _sentinel;    // T must initialize its _next_link=nullptr
  Funnel(const Funnel&) = delete;
  Funnel& operator=(const Funnel&) = delete;
};

template <typename T>
inline Funnel<T>::Funnel() : _head((T*)&_sentinel), _tail((T*)&_sentinel) {
}

template <typename T>
inline bool Funnel<T>::push(T* e) {
  e->_next_link = 0;
  T* prev = rx_atomic_swap(&_head, e);
  prev->_next_link = e;
  return prev == (T*)&_sentinel;
}

template <typename T>
inline T* Funnel<T>::pop() {
  // This is the only function manipulating _tail and we are always called in the same thread
  T* tail = _tail;
  T* next = (T*)tail->_next_link;

  if (tail == (T*)&_sentinel) {
    // First time we are dequeueing
    if (next == 0) {
      // The funnel is empty
      return 0;
    }
    // Here, next == _tail->_next_link
    _tail = next;
    tail = next;
    next = (T*)next->_next_link;
  }

  if (next) {
    // We found an entry
    _tail = next;
    return tail;
  }

  T* head = _head;
  if (tail != head) {
    return 0;
  }

  push((T*)&_sentinel);
  next = (T*)tail->_next_link;

  if (next) {
    _tail = next;
    return tail; // might be nullptr in the case the funnel is empty
  }

  return 0;
}

template <typename T>
template <typename F>
inline void Funnel<T>::foreach(F f) const {
  rx_atomic_barrier();
  T* e = _head;
  T* end = (T*)&_sentinel;
  while (e != end && e) {
    T* e2 = e;
    if (e) {
      e = e->_next_link;
      f(e2);
    }
  }
}

} // namespace rx
