// intrusive: yes, order: FIFO, thread-safety: none, links: 2
#pragma once
#include <assert.h>

template <typename T> struct List {
  // T must comform to:
  struct Entry { Entry* _next_link; Entry* _prev_link; };

  T* head() const { return _head; }
  T* tail() const { return _tail; }
  T* first() const { return _head; }
  bool empty() const { return _head == nullptr; }

  List() {}
    // -> {HEAD=TAIL}

  List(T*); template <typename... Tn> List(T*, Tn...);
    // (e1...) -> {HEAD=e1...=TAIL}

  void push_front(T*); template <typename... Tn> void push_front(T*, Tn...);
    // {HEAD=e3=TAIL}.push_front(e1...) -> {HEAD=e1... e3=TAIL}

  void push_back(T*); template <typename... Tn> void push_back(T*, Tn...);
    // {HEAD=e1=TAIL}.push_back(e2...) -> {HEAD=e1 e2...=TAIL}

  T* pop_front();
    // {HEAD=e1 e2 e3=TAIL}.pop_front() => e1 -> {HEAD=e2 e3=TAIL}

  T* pop_back();
    // {HEAD=e1 e2 e3=TAIL}.pop_back()  => e3 -> {HEAD=e1 e2=TAIL}

  void remove(T*);
    // {HEAD=e1 e2 e3=TAIL}.remove(e2) -> {HEAD=e1 e3=TAIL}

  void insert_after(T*, T* prev);
    // {HEAD=e1 e2=TAIL}.insert_after(e3, e1) -> {HEAD=e1 e3 e2=TAIL}

  void insert_before(T*, T* next);
    // {HEAD=e2 e1=TAIL}.insert_before(e3, e1) -> {HEAD=e2 e3 e1=TAIL}

private:
  T* _head = nullptr;
  T* _tail = nullptr;
};

// ------

template <typename T> inline
List<T>::List(T* e) {
  e->_next_link = nullptr;
  e->_prev_link = nullptr;
  _head = _tail = e;
}

template <typename T>
template <typename... Tn> inline
List<T>::List(T* e, Tn... rest) : List(e) {
  push_back(rest...);
}

template <typename T> inline
void List<T>::push_front(T* e) {
  // {HEAD=TAIL}.push_front(e3) -> {HEAD=e3=TAIL}
  // {HEAD=e3=TAIL}.push_front(e2) -> {HEAD=e2 e3=TAIL}
  // {HEAD=e2 e3=TAIL}.push_front(e1) -> {HEAD=e1 e2 e3=TAIL}
  assert(e != nullptr);
  e->_prev_link = nullptr;
  if (_head != nullptr) {
    _head->_prev_link = e;
  } else {
    _tail = e;
  }
  e->_next_link = _head;
  _head = e;
}

template <typename T>
template <typename... Tn> inline
void List<T>::push_front(T* e, Tn... rest) {
  // {HEAD=TAIL}.push_front(e1, e2) -> {HEAD=e1 e2=TAIL}
  // {HEAD=e3=TAIL}.push_front(e1, e2) -> {HEAD=e1 e2 e3=TAIL}
  push_front(rest...); push_front(e);
}

template <typename T> inline
void List<T>::push_back(T* e) {
  // {HEAD=TAIL}.push_back(e1)
  // {HEAD=e1=TAIL}.push_back(e2)
  // {HEAD=e1 e2=TAIL}.push_back(e3)
  // {HEAD=e1 e2 e3=TAIL}
  assert(e != nullptr);
  e->_next_link = nullptr;
  if (_tail) {
    _tail->_next_link = e;
  } else {
    _head = e;
  }
  e->_prev_link = _tail;
  _tail = e;
}

template <typename T>
template <typename... Tn> inline
void List<T>::push_back(T* e, Tn... rest) {
  // {HEAD=TAIL}.push_back(e1, e2) -> {HEAD=e1 e2=TAIL}
  // {HEAD=e1=TAIL}.push_back(e2, e3) -> {HEAD=e1 e2 e3=TAIL}
  push_back(e); push_back(rest...);
}

template <typename T> inline
T* List<T>::pop_front() {
  // {HEAD=e1 e2 e3=TAIL}.pop_front() => e1
  // {HEAD=e2 e3=TAIL}.pop_front() => e2
  // {HEAD=e3=TAIL}.pop_front() => e3
  // {HEAD=TAIL}.pop_front() => NULL
  T* e = _head;
  if (e != nullptr) {
    if (e == _tail) {
      _head = _tail = nullptr;
    } else {
      _head = (T*)e->_next_link;
    }
    e->_next_link = nullptr;
    e->_prev_link = nullptr;
  }
  return e;
}

template <typename T> inline
T* List<T>::pop_back() {
  // {HEAD=e1 e2 e3=TAIL}.pop_back() => e3
  // {HEAD=e1 e2=TAIL}.pop_back() => e2
  // {HEAD=e1=TAIL}.pop_back() => e1
  // {HEAD=TAIL}.pop_back() => NULL
  T* e = _tail;
  if (e != nullptr) {
    if (e == _head) {
      _head = _tail = nullptr;
    } else {
      _tail = (T*)e->_prev_link;
    }
    e->_next_link = nullptr;
    e->_prev_link = nullptr;
  }
  return e;
}

template <typename T> inline
void List<T>::remove(T* e) {
  // {HEAD=e1 e2 e3=TAIL}.remove(e1) -> {HEAD=e2 e3=TAIL}
  // {HEAD=e1 e2 e3=TAIL}.remove(e2) -> {HEAD=e1 e3=TAIL}
  // {HEAD=e1 e2 e3=TAIL}.remove(e3) -> {HEAD=e1 e2=TAIL}
  T* next = (T*)e->_next_link;
  T* prev = (T*)e->_prev_link;
  if (next != nullptr) {
    assert(next->_prev_link == e);
    next->_prev_link = prev;
  }
  if (prev != nullptr) {
    assert(prev->_next_link == e);
    prev->_next_link = next;
  }
  if (e == _head) {
    _head = next;
  }
  if (e == _tail) {
    _tail = prev;
  }
  e->_next_link = nullptr;
  e->_prev_link = nullptr;
}

template <typename T> inline
void List<T>::insert_after(T* e, T* prev) {
  // {HEAD=e1=TAIL}.insert_after(e2, e1) -> {HEAD=e1 e2=TAIL}
  // {HEAD=e1 e2=TAIL}.insert_after(e3, e1) -> {HEAD=e1 e3 e2=TAIL}
  assert(!empty());
  assert(e != nullptr);
  assert(prev != nullptr);
  e->_prev_link = prev;
  e->_next_link = prev->_next_link;
  if (prev->_next_link) {
    prev->_next_link->_prev_link = e;
  }
  prev->_next_link = e;
  if (_tail == prev) {
    _tail = e;
  }
}

template <typename T> inline
void List<T>::insert_before(T* e, T* next) {
  // {HEAD=e1=TAIL}.insert_before(e2, e1) -> {HEAD=e2 e1=TAIL}
  // {HEAD=e2 e1=TAIL}.insert_before(e3, e1) -> {HEAD=e2 e3 e1=TAIL}
  assert(!empty());
  assert(e != nullptr);
  assert(next != nullptr);
  e->_next_link = next;
  e->_prev_link = next->_prev_link;
  if (next->_prev_link) {
    next->_prev_link->_next_link = e;
  }
  next->_prev_link = e;
  if (_head == next) {
    _head = e;
  }
}


static struct _test { _test() {

  struct E {
    E(int v) : v{v} {}
    E* _next_link = nullptr;
    E* _prev_link = nullptr;
    int v = 0;
  };

  E a{1};
  E b{2};
  E c{3};

  {
    List<E> L;
    // _next_link is toward head
    // _prev_link is toward tail

    assert(L.empty());
    assert(L.head() == nullptr);
    assert(L.tail() == nullptr);

    L.push_back(&a); // [a]
    assert(L.head() == &a);
    assert(L.tail() == &a);
    assert(a._prev_link == nullptr);
    assert(a._next_link == nullptr);

    L.push_back(&b); // [a, b]
    assert(L.head() == &a);
    assert(L.tail() == &b);
    assert(a._prev_link == nullptr);
    assert(a._next_link == &b);
    assert(b._prev_link == &a);
    assert(b._next_link == nullptr);

    L.push_back(&c); // [a, b, c]
    assert(L.head() == &a);
    assert(L.tail() == &c);
    assert(a._prev_link == nullptr);
    assert(a._next_link == &b);
    assert(b._prev_link == &a);
    assert(b._next_link == &c);
    assert(c._prev_link == &b);
    assert(c._next_link == nullptr);

    L.remove(&b); // [a, c]
    assert(L.head() == &a);
    assert(L.tail() == &c);
    assert(a._prev_link == nullptr);
    assert(a._next_link == &c);
    assert(c._prev_link == &a);
    assert(c._next_link == nullptr);
    assert(b._prev_link == nullptr);
    assert(b._next_link == nullptr);

    L.remove(&a); // [c]
    assert(L.head() == &c);
    assert(L.tail() == &c);
    assert(a._prev_link == nullptr);
    assert(a._next_link == nullptr);
    assert(b._prev_link == nullptr);
    assert(b._next_link == nullptr);
    assert(c._prev_link == nullptr);
    assert(c._next_link == nullptr);

    L.remove(&c); // []
    assert(L.empty());
    assert(L.head() == nullptr);
    assert(L.tail() == nullptr);
  }
  {
    List<E> L;

    L.push_back(&a);
    L.push_back(&b);
    L.push_back(&c);
    assert(L.pop_front() == &a);
    assert(L.pop_front() == &b);
    assert(L.pop_front() == &c);
    assert(L.pop_front() == nullptr);
    assert(L.empty());
    assert(L.head() == nullptr);
    assert(L.tail() == nullptr);
  }
  {
    List<E> L;

    L.push_back(&a);
    L.push_back(&b);
    assert(L.head() == &a);
    assert(L.tail() == &b);
    assert(a._prev_link == nullptr);
    assert(a._next_link == &b);
    assert(b._prev_link == &a);
    assert(b._next_link == nullptr);
    L.remove(&a);
    assert(L.empty() == false);
    assert(L.head() == &b);
    assert(L.tail() == &b);
    assert(a._prev_link == nullptr);
    assert(a._next_link == nullptr);
    assert(b._prev_link == nullptr);
    assert(b._next_link == nullptr);
  }


}} __test;

