#pragma once

struct Task;

// Ring buffer-style queue.
struct TaskQ {
  // Add task to after the task furthest from the current task (i.e. the "end")
  void add(Task*);

  // Remove task
  void remove(Task*);

  // Current task (or nullptr if empty)
  Task* curr() { return _curr; }

  // Advance to next task and return that task.
  //
  //  0. [A, b, c]          curr() => A
  //  1. [A, b, c]  next(), curr() => B
  //  2. [a, B, c]  next(), curr() => C
  //  3. [a, b, C]  next(), curr() => A
  //  4. [A, b, c]          curr() => A
  //
  void next();

  // Number of tasks in this queue
  size_t size() const { return _size; }

private:
  Task*  _curr = nullptr;
  size_t _size = 0;
};
