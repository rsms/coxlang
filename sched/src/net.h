#pragma once
#include "ref.h"
#include "time.h"
#include <sys/socket.h> /* struct sockaddr_storage */
#include <string>
#include <functional>

using std::string;

enum NetFam : int {
  NetInet4 = AF_INET,
  NetInet6 = AF_INET6,
  NetUNIX  = AF_UNIX,
};

enum NetType : int {
  NetStream    = SOCK_STREAM,     // stream socket
  NetDGram     = SOCK_DGRAM,      // datagram socket
  NetRaw       = SOCK_RAW,        // raw-protocol interface
  NetSeqPacket = SOCK_SEQPACKET,  // sequenced packet stream
};

// Addr represents a network end point address.
struct Addr {
  virtual string network();  // name of the network
  virtual string toString(); // string form of address
  virtual ~Addr() {};
};

struct PollDesc;

// Network channel
struct NetChan {
  bool init(); // if false, see errno. Nth calls are noop.
  bool close();

  // Misc operations must lockMisc(). Misc operations include functions like
  // setsockopt and setDeadline. They need to use lockMisc() to ensure
  // that they operate on the correct fd in presence of a concurrent close() call
  // (otherwise fd can be closed under their feet).
  // lockMisc() returns `false` if the fd cannot be used.
  bool lockMisc();
  void unlockMisc();

  NetChan() : c{nullptr} {}
  NULLABLE_REF_IMPL(NetChan, c, I)
};

// Create a new NetChan
NetChan net_chan(int fd, NetFam, NetType);

// ———————————————————————————————————————————————————————————————————————————
// socket
// TODO: break out to netsock.h

// System sock address type
using SysSockAddr = struct sockaddr_storage;
#define sys_sockaddrlen(sp) ((size_t)((sp)->ss_len))
inline bool sys_getsockaddr(intptr_t fd, SysSockAddr& sa, uint32_t& slen) {
  return (getsockname(fd, (struct sockaddr*)&sa, &slen) == 0);
}
inline bool sys_getpeeraddr(intptr_t fd, SysSockAddr& sa, uint32_t& slen) {
  return (getpeername(fd, (struct sockaddr*)&sa, &slen) == 0);
}

// A sockaddr represents a TCP, UDP, IP or Unix network endpoint
// address that can be converted into a sockaddr.
struct SockAddr : Addr {
  SockAddr() {};
  SockAddr(const SockAddr&) = default;
  SockAddr(SockAddr&&) = default;
  SockAddr(SysSockAddr&);

  // family returns the platform-dependent address family identifier.
  NetFam family();

  // isWildcard reports whether the address is a wildcard address.
  // bool isWildcard();

  // Sets system address value and size.
  // It returns a zero size when the address is null.
  void sockaddr(NetFam, SysSockAddr&, uint32_t&);
private:
  SysSockAddr _addr;
};

bool netsock_dial(NetChan&, SockAddr* laddr, SockAddr* raddr, Time deadline);

enum SockOpt {
  SockDefault  = 0,
  SockIPv6Only = 1,
};

// socket returns a network file descriptor that is ready for
// asynchronous I/O using the network poller.
NetChan netsock(
  NetFam,
  NetType,
  int proto, // e.g. IPPROTO_*
  SockOpt,
  SockAddr* laddr, // listen address
  SockAddr* raddr, // read address
  Time deadline
);
