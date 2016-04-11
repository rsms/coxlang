#pragma once
#include "taskhandle.h"
#include "cond.h"
#include <functional>

// Task function
using TaskFn = std::function<void()>;

// Start a new task executing function f.
TaskHandle go(TaskFn&& f);

// Yield for other tasks waiting to execute.
// Control is returned immediately after any other tasks have run.
// Calling yield() guarantees that the task is not run until the next call
// to its scheduler's poll().
void yield();

// Sleep for `duration` nanoseconds
void tsleep(uint64_t duration);

// Wait for conditions of file descriptor `fd`.
// Returns a bitmask of the conditions that were met.
Cond awaitCond(int fd, Cond);

// Get a handle to the task that represents the caller
// TaskHandle task();

// Thrown when a task has been canceled. User-catchable.
struct Canceled {};
static Canceled kCanceled;
