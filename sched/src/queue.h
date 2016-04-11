// intrusive: yes, order: FIFO, thread-safety: none, links: 1
#pragma once

template <typename T>
struct Queue {
  Queue() : _head{nullptr}, _tail{nullptr} {} // empty list
  Queue(T* e) {
    _head = _tail = e;
    e->_next_link = nullptr;
  }
  template <typename... TL>
  Queue(T* e, TL... rest) : Queue(e) {
    push(rest...);
  }

  T* head() const { return _head; }
  T* tail() const { return _tail; }
  bool empty() const { return _head == nullptr; }

  // Add item to end of queue
  void push(T* e) {
    assert(e != nullptr);
    e->_next_link = nullptr;
    if (_tail) {
      _tail->_next_link = e;
    } else {
      _head = e;
    }
    _tail = e;
  }

  template <typename... TL>
  void push(T* e, TL... rest) {
    push(e); push(rest...);
  }

  // Remove and return first item in queue
  T* pop() {
    T* e = _head;
    if (e == _tail) {
      _head = nullptr;
      _tail = nullptr;
    } else {
      _head = (T*)e->_next_link;
      e->_next_link = nullptr;
    }
    return e;
  }
protected:
  T* _head;
  T* _tail;
};
