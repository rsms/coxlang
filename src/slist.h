#pragma once
#include <forward_list>

// Intrusive singly-linked list. T is expected to have a field T*nextSib
template <typename T>
struct SListIntr {
  T* first = nullptr;
  T* last = nullptr;

  bool empty() const {
    return first == nullptr;
  }

  // Add to end of list
  void append(T& n) {
    if (first == nullptr) {
      first = &n;
    } else {
      last->nextSib = &n;
    }
    last = &n;
    last->nextSib = nullptr;
  }

  // Add to beginning of list
  void prepend(T& n) {
    n.nextSib = first;
    first = &n;
    if (last == nullptr) {
      last = &n;
    }
  }

  // Add a variable number of items, starting with `firstn`.
  // Consecutive are expected to be linked by ->nextSib, and by null-terminated.
  void appendList(T& firstn) {
    T* n = &firstn;

    if (first == nullptr) {
      last = first = n;
      n = n->nextSib;
    }

    while (n != nullptr) {
      last->nextSib = n;
      last = n;
      n = n->nextSib;
    }
  }
};

// Singly-linked list
template <typename T>
struct SList {
  T* first() const { return _fl.front(); }
  T* last() const { return *_fllast; }
  bool empty() const { return !_init || _fl.empty(); }

  // Add to end of list
  void append(T* p) {
    if (!_init) {
      _fl.push_front(p);
      _fllast = _fl.begin();
      _init = true;
    } else {
      _fllast = _fl.insert_after(_fllast, p);
    }
  }

  void prepend(T* p) {
    _fl.push_front(p);
    if (!_init) {
      _fllast = _fl.begin();
      _init = true;
    }
  }

private:
  bool                  _init = false;
  std::forward_list<T*> _fl;
  typename decltype(_fl)::iterator _fllast;

public:
  // iteration
  typename decltype(_fl)::const_iterator begin() const { return _fl.begin(); }
  typename decltype(_fl)::const_iterator end() const { return _fl.end(); }
};
