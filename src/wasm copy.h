#pragma once
#include "error.h"
#include "ast.h"
// WASM code generator

namespace wasm {

typedef unsigned char byte;
typedef unsigned int  uint32;

enum Type : unsigned char {
  Void = 0,
  i32 = 1,
  i64 = 2,
  f32 = 3,
  f64 = 4,
};

// varint pointer for later rewriting or patching
struct VarUInt32 {
  static constexpr uint32 Future = 0xffffffff;
  byte* p = nullptr;
  void write(uint32);
};

struct Buf {
  byte*     p = nullptr;
  uint32    len = 0;
  uint32    cap = 0;
  VarUInt32 sectlen;
};

// Functions returning a varint pointer accepts Future in place of count,
// in which case the value of count is expected to be written in the future
// by calling write() on the returned varint pointer.

void beginModule(Buf&);
  VarUInt32 beginSignatures(Buf& b, uint32 count);
    void writeSignature(Buf& b, Type result, uint32 nparams, Type* params);
  // ImportTable importTable();
  // FunctionSignatures functionSignatures();
  // IndirectFunctionTable indirectFunctionTable();
  // Memory memory();
  // ExportTable exportTable();
  // StartFunction startFunction();
  // FunctionBodies functionBodies();
  // DataSegments dataSegments();
  // Names names();
void endModule(Buf&);


Module module(Buf&);


Err emit_module(Buf&, AstNode&);


} // namespace wasm


// struct WCode {
//   uint32_t len = 0;
//   uint32_t cap = 0;
//   char*    p = nullptr;

//   void reserve(size_t nbytes) {
//     if (cap - len < nbytes) {
//       cap += 4096;
//       p = (char*)realloc(p, cap);
//     }
//   }

//   void writec(char c) {
//     p[len++] = c;
//   }

//   void writeu32(uint32_t u) {
//     *((uint32_t*)(p+len)) = u;
//     len += 4;
//   }

//   void writev(const char* pch, uint32_t length) {
//     memcpy((void*)(p+len), (const void*)pch, length);
//     len += length;
//   }
// };

// Err wasm_genmod(WCode&, AstNode& prog);

