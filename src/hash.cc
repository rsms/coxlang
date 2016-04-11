#include "hash.h"

namespace hash {

// Specialized 128-bit Base64 codec from https://gist.github.com/rsms/6433149

template <typename T, typename X> inline static T bitset(T n1, X n0) {
  // Returns a value with specific bits set:
  //   n1 set bits followed by n0 unset bits
  // bitset(char(4), 2) -> 00111100
  return ~(~0 << n1) << n0;
  // As seen in "Bitwise Operators" of "The C Programming Language, second edition", and
  // "Space Efficiency" of "The Practice of Programming".
}

static const char BASE64_CHARS[] = {
  '0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F',
  'G','H','I','J','K','L','M','N','O','P','Q','R','S','T','U','V',
  'W','X','Y','Z','a','b','c','d','e','f','g','h','i','j','k','l',
  'm','n','o','p','q','r','s','t','u','v','w','x','y','z','-','_'};

//static const char BASE64_VALUES[] = {
// -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
// -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,-1,-1,-1,-1,-1,-1,
// -1,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,-1,-1,-1,-1,63,
// -1,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,
// -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
// -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
// -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
// -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};

// See https://gist.github.com/rsms/6418070 for using different mappings

static void base64_encode_B16(const B16& id, char buf[22]) {
  // Base64 encoding is pretty simple, with a 6-bit stride and a repeating pattern every 3 bytes:
  //                                               ••••••|•• ••••|•••• ••|••••••| repeat
  // --B--  01234567 01234567 01234567 01234567    012345 01 2345 0123 45 012345
  // 0..3   •••••••• •••••••• •••••••• ••••••••    ••••••|•• ••••|•••• ••|••••••| ••••••|••
  // 4..7   •••••••• •••••••• •••••••• ••••••••    ••••|•••• ••|••••••| ••••••|•• ••••|••••
  // 8..11  •••••••• •••••••• •••••••• ••••••••    ••|••••••| ••••••|•• ••••|•••• ••|••••••
  // 12..15 •••••••• •••••••• •••••••• ••••••••    |••••••|•• ••••|•••• ••|••••••| ••••••••
  //
  //                 Bits in chunks:   012345 01 2345 0123 45 012345
  //                        Chunk #:   —— 0 —|—— 1 ——|—— 2 ——|—— 3 —|
  //                     Chunk mask:     c0   c1a c1b c2a c2b=c1a c3=c0
  //                            0..3   ••••••|•• ••••|•••• ••|••••••|  Pass 1
  //                            4..7   ••••••|•• ••••|•••• ••|••••••|  Pass 2
  //                            8..11  ••••••|•• ••••|•••• ••|••••••|  Pass 3
  //                            12..15 ••••••|•• ••••|•••• ••|••••••|  Pass 4
  //                            16..19 ••••••|•• ••••|•••• ••|••••••|  Pass 5
  //                            20..21 ••••••|•• xxxx|                 Pass 6
  //                            22..23                xxxx xx|xxxxxx|
  //
  // On a 2.8 GHz Intel Core i7 and clang 4.2 -O3 -std=c++11 this was measured to perform well:
  //   Avg nsec/invocation:            39
  //   Avg invocations/second: 25 372 038
  //   Iterations:             10 000 000
  // These numbers include C function call overhead (pusing and popping the stack).
  // Basically this function becomes a sequence of a few logical instructions like SHL, AND, etc
  // with a single CMP and a single JUMP (speaking of x86 here).
  //
  const long b00000011 = bitset(2, 0);
  const long b11110000 = bitset(4, 4);
  const long b00001111 = bitset(4, 0);
  const long b11000000 = bitset(2, 6);
  const long b00111111 = bitset(6, 0);

  char* p = buf;
  for (size_t srci = 0; ; srci += 3) {
    long src0 = id.bytes[srci]; // Upsize from i8 to long (e.g. MOVZB)
    *p++ = BASE64_CHARS[src0 >> 2];
      // chunk0 = (mask src[0] 00111111)
    if (srci == 15) {
      // Last pass is partial
      *p = BASE64_CHARS[(src0 & b00000011) << 4];
      return;
    }
    long src1 = id.bytes[srci+1];
    *p++ = BASE64_CHARS[ ((src0 & b00000011) << 4) | ((src1 & b11110000) >> 4) ];
      // chunk1 = (combine
      //   (bit-downsize 4 (mask src[0] 00000011))  #-> 00110000
      //   (bit-upsize 4 (mask src[1] 11110000))    #-> ????1111
      // )
    long src2 = id.bytes[srci+2];
    *p++ = BASE64_CHARS[ ((src1 & b00001111) << 2) | ((src2 & b11000000) >> 6) ];
    *p++ = BASE64_CHARS[src2 & b00111111];
  }
}


void encode_128(const B16& r, char buf[22]) {
  base64_encode_B16(r, buf);
}

std::string encode_128(const B16& r) {
  std::string outs;
  outs.resize(22);
  encode_128(r, (char*)outs.data());
  return std::move(outs);
}


} // namespace
