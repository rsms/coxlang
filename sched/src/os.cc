#include "os.h"
#include <fcntl.h>
#include <errno.h>

int closeonexec(int fd) {
  int r;
  while ((r = fcntl(fd, F_SETFD, FD_CLOEXEC)) == -1 && errno == EINTR) {}
  return r;
}

int nonblock(int fd) {
  int r;
  while ((r = fcntl(fd, F_SETFL, O_NONBLOCK)) == -1 && errno == EINTR) {}
  return r;
  // Note: We don't care about the other two flags to SETFL (O_APPEND, O_ASYNC.)
  // If we ever do, we can enable this read-modify-write code:
  // int r, flags;
  // do {
  //   r = fcntl(fd, F_GETFL);
  // } while (r == -1 && errno == EINTR);
  // if (r != -1 && !(r & O_NONBLOCK)) {
  //   flags = r | O_NONBLOCK;
  //   do {
  //     r = fcntl(fd, F_SETFL, flags);
  //   } while (r == -1 && errno == EINTR);
  // }
  // return r;
}
