

int main() {
  Chan<string> ch;
  auto consumer = go [&]{
    while (1) {
      string s;
      if (!ch.recv(s)) {
        break;
      }
      println(s.toUpperCase());
    }
  }
  auto producer = go [&]{
    while (1) {
      string s
      if (!readio(std::cin, s)) {
        break;
      }
      ch.send(s);
    }
  }
  await(producer)
}
/*

========== T0 ==========   ========== T1 ==========   ========== T2 ==========
schedule consumer on T1    .                          .
schedule producer on T2    pick up consumer           .
await producer             resume consumer            pick up producer
  suspend                    call ch.recv             resume producer
.                              suspend                  call readio
.                          .                              suspend
.                          .                          .
.                          .                          .
.                          .                          <readio has data>
.                          .                          resume producer
.                          .                            call ch.send
.                          <recv has data>              call readio
.                          resume consumer                suspend
.                            call println             .
.                            call ch.recv             .
.                              suspend                .
.                          .                          .
.                          .                          .
<await has data>           .                          .
resume main
end consumer, producer     <recv canceled>            .
  suspend                  resume consumer            <readio canceled>
.                            return (task ends)       resume producer
.                                                       return (task ends)
<end done>
resume main
return (program ends)
*/
