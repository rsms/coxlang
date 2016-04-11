
This is a subproject of Co that implements the Go scheduler in C++. Although not yet, complete, it does implement:

- coroutines with fast C stack switching
- P, M and T primitives
- partial work stealing
- netpoll (currently only for kqueue)

Example:

```cc
int main(/*int argc, char const *argv[]*/) {
  gs_bootstrap();

  go2([]{
    log("<task 1>: enter");
    SockAddr raddr; // inet 127.0.0.1:1337
    NetChan c = netsock(NetInet4, NetStream, 0, SockDefault, nullptr, &raddr, 0);
    if (!c) {
      rxlog("<task 1>: netsock failed: " << strerror(errno));
      return;
    }
    log("<task 1>: connected");
    log("<task 1>: exit");
  });

  log("<task 0>: exit");
  return 0;
}
```

```sh
$ node misc/echo-server.js &
listening at tcp://127.0.0.1:1337
$ ./build/bin/cox
<task 1>: enter [src/cox.cc:129]
netsock_connect: netpoll_await [src/net.cc:319]
netpoll_await: parking task [src/netpoll.cc:141]
t_park: IO wait T@0x7fd36b5000e0 [src/t.cc:99]
go2: returned to t0 [src/t.cc:223]
m_schedule [src/m.cc:114]
gs_findrunnable: netpoll_poll(PollImmediate) [src/gs.cc:175]
netpoll_poll: got WRITE event for fd 3 [src/netpoll.cc:370]
netpoll_ready: mode=w [src/netpoll.cc:196]
netpoll_unblock(w): pd.rt=0x0, pd.wt=0x7fd36b5000e0 [src/netpoll.cc:166]
m_execute: T@0x7fd36b5000e0 [src/m.cc:39]
<task 1>: connected [src/cox.cc:137]
<task 1>: exit [src/cox.cc:139]
netchan_close [src/net.cc:59]
echo-server: established connection 1
echo-server: lost connection 1
$
```
