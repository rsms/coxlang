// #include "cox.h"
#include "log.h"
#include "netpoll.h"
#include "debug.h"
#include "t.h"
#include "m.h"
#include "p.h"
#include "gs.h"
#include "net.h"

#include <stdio.h>
#include <signal.h>
#include <assert.h>
#include <iostream>
#include <functional>

#include <sys/socket.h>       /*  socket definitions        */
#include <sys/types.h>        /*  socket types              */
#include <arpa/inet.h>        /*  inet (3) funtions         */
#include <fcntl.h>

struct DebugDealloc {
  DebugDealloc(const std::string& name) : _name{name} {}
  ~DebugDealloc() { rxlog("~DebugDealloc " << _name); }
  std::string _name;
};

bool SchedPoll();


int SockCreate(int type) {
  #if defined(SOCK_NONBLOCK) && defined(SOCK_CLOEXEC)
    type = type | SOCK_NONBLOCK | SOCK_CLOEXEC
  #endif
  ;

  int fd = socket(AF_INET, type, 0);
  if (fd == -1) {
    return fd;
  }

  #if defined(SO_NOSIGPIPE)
  {
    int on = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on)) == -1) {
      return -1;
    }
  }
  #endif

  // Make non-blocking
  #if !defined(SOCK_NONBLOCK) || !defined(SOCK_CLOEXEC)
  int r, flags;
  do {
    r = fcntl(fd, F_GETFL);
  } while (r == -1 && errno == EINTR);
  if (r != -1 && !(r & O_NONBLOCK)) {
    flags = r | O_NONBLOCK;
    do {
      r = fcntl(fd, F_SETFL, flags);
    } while (r == -1 && errno == EINTR);
  }
  if (r == -1) { return -1; }

  do {
    r = fcntl(fd, F_GETFD);
  } while (r == -1 && errno == EINTR);
  if (r != -1 && !(r & FD_CLOEXEC)) {
    flags = r | FD_CLOEXEC;
    do {
      r = fcntl(fd, F_SETFD, flags);
    } while (r == -1 && errno == EINTR);
  }
  if (r == -1) { return -1; }
  #endif

  return fd;
}

int SockConnect(int fd, struct sockaddr* addr, socklen_t addrlen) {
  if (connect(fd, addr, addrlen) == 0) {
    return 0;
  }
  if (errno != EINPROGRESS) {
    return -1;
  }

  // connection is underway -- suspend calling task until fd is writable
  PollDesc* pd = netpoll_open(fd);
  if (!pd) {
    rxlog("netpoll_open failed: %s", strerror(errno));
  }

  rxlog("SockConnectINet4: netpoll_await (connect => EINPROGRESS)");
  if (netpoll_await(*pd, 'w', PollBlocking)) {
    socklen_t stz = sizeof(errno);
    getsockopt(fd, SOL_SOCKET, SO_ERROR, &errno, &stz);
    if (errno != 0) {
      return -1;
    }
    return 0;
  }
  return -1;
}


int SockConnectINet4(int fd, const char* host, int port) {
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (host != nullptr) {
    if (inet_aton("127.0.0.1", &addr.sin_addr) == -1) {
      return -1;
    }
  } else {
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  }

  return SockConnect(fd, (struct sockaddr*)&addr, sizeof(addr));
}


int main(/*int argc, char const *argv[]*/) {
  // TaskHandle h1;
  gs_bootstrap();

  go2([]{
    rxlog("<task 1>: enter");

    SockAddr raddr; // inet 127.0.0.1:1337
    NetChan c = netsock(NetInet4, NetStream, 0, SockDefault, nullptr, &raddr, 0);
    if (!c) {
      rxlog("<task 1>: netsock failed: " << strerror(errno));
      return;
    }
    rxlog("<task 1>: connected");
    
    rxlog("<task 1>: exit");
  });

  while (1) {
    bool inheritTime;
    T* t = gs_findrunnable(inheritTime);
    rxlog("main: gs_findrunnable => " << t);
    if (t == nullptr) {
      break;
    }
  }

  gs_maincancel();
  return 0;

  // go([]{
  //   rxlog("<task 1>: enter");
  //   int socket = SockCreate(SOCK_STREAM); assert(socket != -1);
  //   rxlog("<task 1>: connecting to tcp:127.0.0.1:1337");
  //   if (SockConnectINet4(socket, nullptr, 1337) == -1) {
  //     rxlog("<task 1>: failed to connect: " << strerror(errno));
  //   } else {
  //     rxlog("<task 1>: connected");
  //   }
  //   //auto r = DebugReadTCP();
  //   // rxlog("<task 1>: readtcp returned: " << r);
  //   rxlog("<task 1>: exit");
  // });

  // go([h1=&h1]{
  //   rxlog("<task 1>: enter");
  //   go([]{
  //     DebugDealloc dd1{"<task 2>"};
  //     rxlog("<task 2>: enter");
  //     rxlog("<task 2>: yielding");
  //     try {
  //       yield();
  //       rxlog("<task 2>: resumed");
  //     } catch (Canceled&) {
  //       rxlog("<task 2>: canceled");
  //       return;
  //     }
  //     rxlog("<task 2>: exit");
  //   });
  //   // task 3 will end when we clear the handle h1
  //   *h1 = go([h1] {
  //     rxlog("<task 3>: enter");
  //     while (1) {
  //       rxlog("<task 3>: yielding");
  //       yield();
  //       rxlog("<task 3>: resumed");
  //       *h1 = nullptr;
  //     }
  //     rxlog("<task 3>: exit");
  //   });
  //   rxlog("<task 1>: exit");
  // });

  // go([]{
  //   rxlog("<task 4>: enter, yield");
  //   yield();
  //   rxlog("<task 4>: resume, exit");
  // });

  // while (SchedPoll()) {}
  rxlog("main: exiting");
  return 0;
}
