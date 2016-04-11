#include "common.h"
#include "taskhandle.h"
#include "task.h"

TaskHandle::TaskHandle(Task* T)
  : _T{T->retainRef()}
{}

TaskHandle::TaskHandle(const TaskHandle& h)
  : _T{h._T == nullptr ? nullptr : h._T->retainRef()}
{}

TaskHandle::~TaskHandle() {
  if (_T != nullptr) {
    _T->releaseRef();
    _T = nullptr;
  }
}

TaskHandle& TaskHandle::operator=(const TaskHandle& h) {
  auto T2 = _T;
  if ((_T = h._T) != nullptr) {
    _T->retainRef();
  }
  if (T2 != nullptr) {
    T2->releaseRef();
  }
  return *this;
}

TaskHandle& TaskHandle::operator=(Task* T) {
  auto T2 = _T;
  if ((_T = T) != nullptr) {
    _T->retainRef();
  }
  if (T2 != nullptr) {
    T2->releaseRef();
  }
  return *this;
}

