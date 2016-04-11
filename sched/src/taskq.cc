#include "common.h"
#include "taskq.h"
#include "task.h"

void TaskQ::add(Task* t) {
  t->_next_link = nullptr;
  if (_curr == nullptr) {
    // first task. [] -> [A]
    _curr = t;
    t->_next_link = t;
    t->_prev_link = t;
  } else {
    if (_curr->_prev_link == _curr) {
      // single task. [A] -> [A, t]
      t->_prev_link = _curr; // t.prev=A
      t->_next_link = _curr; // t.next=A
      _curr->_prev_link = t; // A.prev=t
      _curr->_next_link = t; // A.next=t
    } else {
      // multiple tasks. [a, B, c, d] -> [a, t, B, c, d]
      t->_next_link = _curr;        // t.next=B
      t->_prev_link = _curr->_prev_link; // t.prev=a
      _curr->_prev_link->_next_link = t; // a.next=t
      _curr->_prev_link = t;        // B.prev=t
    }
  }
  ++_size;
}


void TaskQ::remove(Task* t) {
  if (t->_next_link == t) {
    // only task.
    _curr = nullptr;
  } else if (t->_prev_link->_next_link == t) {
    // one other task.
    // 1. [a, B]  remove(B),
    // 2. [A]
    _curr = (Task*)t->_prev_link->_next_link;
    _curr->_prev_link = _curr; // a.prev=a
    _curr->_next_link = _curr; // a.next=a
  } else {
    // many tasks.
    // 1. [a, B, c]  remove(B),
    // 2. [a, C]
    t->_prev_link->_next_link = t->_next_link; // a.next=c
    t->_next_link->_prev_link = t->_prev_link; // c.prev=a
    if (_curr == t) {
      _curr = (Task*)t->_next_link; // c -> C
    }
  }
  t->_next_link = nullptr;
  t->_prev_link = nullptr;
  --_size;
}


void TaskQ::next() {
  if (_curr != nullptr) {
    _curr = (Task*)_curr->_next_link;
  }
}
