#pragma once

struct Task;

// TaskHandle represents a reference to a task.
// While there are handles to a task, that task is guaranteed to live,
// even if it ends before the last handle is deleted.
//
// When the last handle to a task is deleted, the task is canceled.
// This means that ~TaskHandle might suspend the calling task (i.e. the
// task which causes the last handle to be deleted), but it will never
// truly block.
//
// A task parent holds a handle to a task child, meaning that if there
// are no user-code TaskHandles, a task is canceled when its parent
// ends.
//
struct TaskHandle {
  TaskHandle();
  TaskHandle(Task*);
  TaskHandle(const TaskHandle& h);
  TaskHandle(TaskHandle&&);
  ~TaskHandle();
  TaskHandle& operator=(const TaskHandle&);
  TaskHandle& operator=(TaskHandle&&);
  TaskHandle& operator=(Task*);

private:
  Task* _T;
};

// ——————————————————————————————————————————————————————————————————

inline TaskHandle::TaskHandle()
  : _T{nullptr}
{}

inline TaskHandle::TaskHandle(TaskHandle&& h)
  : _T{h._T}
{
  h._T = nullptr;
}

inline TaskHandle& TaskHandle::operator=(TaskHandle&& h) {
  _T = h._T;
  h._T = nullptr;
  return *this;
}
