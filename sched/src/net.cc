#include "net.h"
#include "netpoll.h"
#include "log.h"
#include "common.h"
#include "target.h"
#include "defer.h"
#include "fdmutex.h"
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <fcntl.h>

struct NetChan::I : RefCounted {
  // locking/lifetime of sysfd + serialize access to Read and Write methods
  FdMutex     fdmutex;

  intptr_t    fd = -1;             // system descriptor
  NetFam      family;
  NetType     type;
  bool        isConnected = false;
  Addr*       laddr = nullptr;
  Addr*       raddr = nullptr;
  PollDesc*   pd = nullptr;        // netpoll descriptor

  ~I();
};

void NetChan::__dealloc(NetChan::I* self) {
  rxlog("NetChan::__dealloc self=" << self);
  delete self;
}


bool NetChan::init() {
  if (c->pd) {
    return true;
  }
  assert(c->fd != -1);
  c->pd = netpoll_open(c->fd);
  return c->pd != nullptr;
}


// Add a reference to this fd.
// Returns an error if the fd cannot be used.
bool netchan_lockMisc(NetChan::I& c) {
  if (!c.fdmutex.incref()) {
    return false;
  }
  return true;
}

// Remove a reference to this FD and close if we've been asked to do so
// (and there are no references left).
void netchan_unlockMisc(NetChan::I& c) {
  rxlog("netchan_unlockMisc");
  if (c->fdmutex.decref()) {
    netchan_destroy(*c);
  }
}

bool NetChan::lockMisc() { return netchan_lockMisc(*c); }
void NetChan::unlockMisc() { netchan_unlockMisc(*c); }


static void netchan_destroy(NetChan::I& c) {
  rxlog("netchan_destroy");
  if (c.pd) {
    // Poller may want to unregister fd in readiness notification mechanism,
    // so this must be executed before closeFunc.
    netpoll_close(*c.pd);
  }
  ::close((int)c.fd);
  c.fd = -1;
  // TODO: runtime.SetFinalizer(fd, nil)
}


static bool netchan_close(NetChan::I& c) {
  // This functions is called from
  //  - NetChan::close when explicitly closed
  //  - NetChan::I::~I when the last reference to a NetChan disappears
  rxlog("netchan_close");
  if (!c.fdmutex.increfAndClose()) {
    rxlog("netchan_close: return errClosing");
    // abort();
    return /*errClosing*/ false;
  }

  defer [&]{
    if (c.fdmutex.decref()) {
      netchan_destroy(c);
    }
  };

  // Unblock any I/O.  Once it all unblocks and returns,
  // so that it cannot be referring to fd.sysfd anymore,
  // the final decref will close fd.sysfd.  This should happen
  // fairly quickly, since all the I/O is non-blocking, and any
  // attempts to block in the pollDesc will return errClosing.
  if (c.pd) {
    c.pd->evict();
  }
  return true;
}


bool NetChan::close() { return netchan_close(*c); }


NetChan::I::~I() {
  rxlog("NetChan::I::~I: this=" << this);
  netchan_close(*this); // ignore error (already closing)
  // TODO: should we move these to close()?
  if (laddr) {
    delete laddr; laddr = nullptr;
  }
  if (raddr) {
    delete raddr; raddr = nullptr;
  }
}



NetChan net_chan(int fd, NetFam family, NetType type) {
  auto c = new NetChan::I;
  rxlog("net_chan: new NetChan::I @ " << c);
  c->fd     = fd;
  c->family = family;
  c->type   = type;
  return NetChan(c);
}

// name of the network
string Addr::network() { return string{}; }

// string form of address
string Addr::toString() { return string{}; }

// ———————————————————————————————————————————————————————————————————————————
// SockAddr


// go uses a set of different functions, each creating a struct
// adhering to the address interface.
//
// func (fd *netFD) addrFunc() func(syscall.Sockaddr) Addr {
//   switch fd.family {
//   case syscall.AF_INET, syscall.AF_INET6:
//     switch fd.sotype {
//     case syscall.SOCK_STREAM:
//       return sockaddrToTCP
//     case syscall.SOCK_DGRAM:
//       return sockaddrToUDP
//     case syscall.SOCK_RAW:
//       return sockaddrToIP
//     }
//   case syscall.AF_UNIX:
//     switch fd.sotype {
//     case syscall.SOCK_STREAM:
//       return sockaddrToUnix
//     case syscall.SOCK_DGRAM:
//       return sockaddrToUnixgram
//     case syscall.SOCK_SEQPACKET:
//       return sockaddrToUnixpacket
//     }
//   }
//   return func(syscall.Sockaddr) Addr { return nil }
// }


SockAddr::SockAddr(SysSockAddr& sa) : _addr{sa} {
}


void SockAddr::sockaddr(NetFam family, SysSockAddr& sa, uint32_t& size) {
  if (family == AF_INET) {
    struct sockaddr_in& a = *(struct sockaddr_in*)&sa;
    size = uint32_t(sizeof(a));
    memset(&a, 0, sizeof(a));
    a.sin_len = (decltype(a.sin_len))size;
    a.sin_family = (int)family;
    a.sin_port   = htons(1337);
    // if (host != nullptr) {
    //   if (inet_aton("127.0.0.1", &addr.sin_addr) == -1) {
    //     return -1;
    //   }
    // } else {
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    // }
  } else {
    TODO_SECTION;
  }
}

// ———————————————————————————————————————————————————————————————————————————
// socket

static inline bool setsockoptInt(int fd, int lvl, int opt, int val) {
  return setsockopt(fd, lvl, opt, &val, sizeof(val)) != -1;
}


static bool setDefaultSockopts(int fd, int family, int type, bool ipv6only) {
  if (family == AF_INET6 && type != SOCK_RAW) { // TODO: only if supportsIPv4map
    // Allow both IP versions even if the OS default
    // is otherwise.  Note that some operating systems
    // never admit this option.
    if (!setsockoptInt(fd, IPPROTO_IPV6, IPV6_V6ONLY, int(ipv6only))) {
      return false;
    }
  }

  // Ignore SIGPIPE
  #if defined(SO_NOSIGPIPE)
  int on = 1;
  if (!setsockoptInt(fd, SOL_SOCKET, SO_NOSIGPIPE, 1)) {
    return false;
  }
  #endif

  // Allow broadcast.
  return setsockoptInt(fd, SOL_SOCKET, SO_BROADCAST, 1);
}

static bool setDefaultListenerSockopts(int fd) {
  // Allow reuse of recently-used addresses.
  return setsockoptInt(fd, SOL_SOCKET, SO_REUSEADDR, 1);
}

static bool setDefaultMulticastSockopts(int fd) {
  return (
    // Allow multicast UDP and raw IP datagram sockets to listen
    // concurrently across multiple listeners.
    setsockoptInt(fd, SOL_SOCKET, SO_REUSEADDR, 1) &&

    // Allow reuse of recently-used ports.
    // This option is supported only in descendants of 4.4BSD,
    // to make an effective multicast application that requires
    // quick draw possible.
    setsockoptInt(fd, SOL_SOCKET, SO_REUSEPORT, 1)
  );
}

static bool closeOnExec(int fd) {
  int r;
  while ((r = fcntl(fd, F_SETFD, FD_CLOEXEC)) == -1 && errno == EINTR) {}
  return r != -1;
}

static bool setNonblock(int fd, bool nonblocking) {
  int r;
  while ((r = fcntl(fd, F_GETFL)) == -1 && errno == EINTR) {}
  if (r == -1) {
    return false;
  }

  if ((nonblocking && !(r & O_NONBLOCK)) || (!nonblocking && (r & O_NONBLOCK))) {
    int flags = r;
    if (nonblocking) {
      flags |= O_NONBLOCK;
    } else {
      flags &= ~O_NONBLOCK;
    }
    while ((r = fcntl(fd, F_SETFL, flags)) == -1 && errno == EINTR) {}
  }

  return r != -1;
}

static int mksocket(int family, int type, int proto) {
  #if defined(SOCK_NONBLOCK) && defined(SOCK_CLOEXEC)
    type |= SOCK_NONBLOCK | SOCK_CLOEXEC;
  #endif
  return socket(family, type, proto);
}


bool netsock_connect(NetChan& c, SysSockAddr* la, SysSockAddr* ra, Time deadline) {
  rxlog("netsock_connect: sys_sockaddrlen(ra)=" << sys_sockaddrlen(ra));

  int r = connect(c->fd, (const struct sockaddr*)ra, sys_sockaddrlen(ra));

  switch (r == 0 ? 0 : errno) {
    case EINPROGRESS: case EALREADY: case EINTR:
      // Connection underway, already connecting or canceled
      break;
    case 0: case EISCONN:
      // The socket is already connected
      // TODO:
      // if !deadline.IsZero() && deadline.Before(time.Now()) {
      //   return errTimeout
      // }
      return c.init();

    #if RX_TARGET_OS_SOLARIS
    case syscall.EINVAL:
      // On Solaris we can see EINVAL if the socket has
      // already been accepted and closed by the server.
      // Treat this as a successful connection--writes to
      // the socket will see EOF.  For details and a test
      // case in C see https://golang.org/issue/6828.
      return true;
    #endif

    default:
      return false;
  }

  if (!c.init()) {
    return false;
  }

  while (1) {
    // Performing multiple connect system calls on a
    // non-blocking socket under Unix variants does not
    // necessarily result in earlier errors being
    // returned. Instead, once runtime-integrated network
    // poller tells us that the socket is ready, get the
    // SO_ERROR socket option to see if the connection
    // succeeded or failed. See issue 7474 for further
    // details.
    rxlog("netsock_connect: netpoll_await");
    assert(c->pd != nullptr);
    if (!netpoll_await(*c->pd, 'w', PollBlocking)) {
      return false;
    }
    socklen_t stz = sizeof(errno);
    if (getsockopt(c->fd, SOL_SOCKET, SO_ERROR, &errno, &stz) == -1) {
      return false;
    }
    switch (errno) {
      case EINPROGRESS: case EALREADY: case EINTR:
        break; // try again
      case 0: case EISCONN:
        return true;
      default:
        return false;
    }
  }
}


bool netsock_dial(NetChan& c, SockAddr* laddr, SockAddr* raddr, Time deadline) {
  SysSockAddr lsa;
  uint32_t    lsalen = 0;

  if (laddr != nullptr) {
    laddr->sockaddr(c->family, lsa, lsalen);
    if (lsalen == 0) {
      return false;
    }
    TODO_SECTION;
    // if err := bind(c.fd, lsa); err != nil {
    //   return os.NewSyscallError("bind", err)
    // }
  }

  SysSockAddr rsa;
  uint32_t    rsalen = 0;

  if (raddr != nullptr) {
    raddr->sockaddr(c->family, rsa, rsalen);
    if (rsalen == 0) {
      return false;
    }
    // Note: netsock_connect implicitly calls c.init()
    if (!netsock_connect(c, (laddr ? &lsa : nullptr), &rsa, deadline)) {
      rxlog("netsock_dial: netsock_connect => " << strerror(errno));
      return false;
    }
    c->isConnected = true;

  } else {
    TODO_SECTION;
    // if err := c.init(); err != nil {
    //   return err
    // }
  }

  if (!sys_getsockaddr(c->fd, lsa, lsalen)) {
    panic("sys_getsockaddr");
  }
  c->laddr = new SockAddr(lsa);

  if (sys_getpeeraddr(c->fd, rsa, rsalen)) {
    c->raddr = new SockAddr(lsa);
  } else if (raddr) {
    c->raddr = new SockAddr(*raddr);
  } else {
    c->raddr = nullptr;
  }

  return true;
}


// socket returns a network file descriptor that is ready for
// asynchronous I/O using the network poller.
// TODO: See `func internetSocket`
NetChan netsock(
  NetFam family,
  NetType type,
  int proto,
  SockOpt opt,
  SockAddr* laddr,
  SockAddr* raddr,
  Time deadline
  // cancel <-chan struct{}
) {
  int fd = mksocket(family, type, proto);
  if (fd == -1) {
    return nullptr;
  }

  if (!setDefaultSockopts(fd, family, type, opt == SockIPv6Only) ||
      !setNonblock(fd, true) ||
      !closeOnExec(fd))
  {
    int e = errno;
    close(fd);
    errno = e;
    return nullptr;
  }

  NetChan c = net_chan(fd, family, type);

  // This function makes a network file descriptor for the
  // following applications:
  //
  // - An endpoint holder that opens a passive stream
  //   connection, known as a stream listener
  //
  // - An endpoint holder that opens a destination-unspecific
  //   datagram connection, known as a datagram listener
  //
  // - An endpoint holder that opens an active stream or a
  //   destination-specific datagram connection, known as a
  //   dialer
  //
  // - An endpoint holder that opens the other connection, such
  //   as talking to the protocol stack inside the kernel
  //
  // For stream and datagram listeners, they will only require
  // named sockets, so we can assume that it's just a request
  // from stream or datagram listeners when laddr is not nil but
  // raddr is nil. Otherwise we assume it's just for dialers or
  // the other connection holders.

  if (laddr != nullptr && raddr == nullptr) {
    TODO_SECTION;
    // switch sotype {
    // case syscall.SOCK_STREAM, syscall.SOCK_SEQPACKET:
    //   if err := fd.listenStream(laddr, listenerBacklog); err != nil {
    //     fd.Close()
    //     return nil, err
    //   }
    //   return fd, nil
    // case syscall.SOCK_DGRAM:
    //   if err := fd.listenDatagram(laddr); err != nil {
    //     fd.Close()
    //     return nil, err
    //   }
    //   return fd, nil
    // }
  }

  if (!netsock_dial(c, laddr, raddr, deadline)) {
    rxlog("netsock: netsock_dial => " << strerror(errno));
    return nullptr;
  }

  return c;
}
