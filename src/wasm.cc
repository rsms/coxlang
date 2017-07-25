#include "wasm.h"
#include <assert.h>

namespace wasm {

// Each section is identified by an immediate string.
// Sections whose identity is unknown to the WebAssembly implementation
// are ignored and this is supported by including the size in bytes for
// all sections. The encoding of all sections begins as follows:
//
// FIELD   TYPE       DESCRIPTION
// size    varuint32  size of this section in bytes, excluding this size
// id_len  varuint32  section identifier string length
// id_str  bytes      section identifier string of id_len bytes
//
// The end of the last present section must coincide with the last byte
// of the module. The shortest valid module is 8 bytes
// (magic number, version, followed by zero sections).
//
// Each section function below declares the header for its section (secthead).

// —————————————————————————————————————————————————————————————————————————

template <uint32 N> // assumes v has a nul byte at the end
static inline constexpr uint32 strlen_cx(const byte(&)[N]) {
  return N-1;
}

static void grow(Buf& b, uint32 nbytes) {
  // Grows b to fit at least nbytes.
  // Invalidates b.startp, b.endp and b.p.
  // grows by rounding nbytes upward in steps of GrowSize; e.g.
  // GrowSize=512 nbytes=1:    grows by 512 bytes,
  // GrowSize=512 nbytes=513:  grows by 1024 bytes,
  // GrowSize=512 nbytes=1025: grows by 1536 bytes,
  // GrowSize=512 nbytes=1537: grows by 2048 bytes,
  // ... and so on.
  size_t needz = size_t((nbytes + Buf::GrowSize - 1) & ~(Buf::GrowSize - 1));
  size_t size = (b.endp - b.startp) + needz;
  size_t offs = b.p - b.startp;
  b.startp = (byte*)realloc(b.startp, size);
  b.endp   = b.startp + size;
  b.p      = b.startp + offs;
}

static inline void reserve(Buf& b, uint32 nbytes) {
  if (b.endp - b.p < nbytes) {
    grow(b, nbytes);
  }
}

static inline void writeb(Buf& b, byte c) {
  *b.p = c;
  b.p++;
}

static inline void writeu32(Buf& b, uint32 u) {
  *((uint32*)b.p) = u;
  b.p += 4;
}

static inline void writev(Buf& b, const byte* p, uint32 len) {
  memcpy(b.p, p, len);
  b.p += len;
}

// Read a LEB128-encoded value at p where p has len bytes available.
// p must have at least 4 bytes available.
// Returns p + <number of bytes written>.
static const byte* read_varuint32(const byte* p, uint32 len, uint32& value) {
  uint32 result = *p++;
  if (result > 0x7f && len > 1) {
    int cur = *p++;
    result = (result & 0x7f) | ((cur & 0x7f) << 7);
    if (cur > 0x7f && len > 2) {
      cur = *p++;
      result |= (cur & 0x7f) << 14;
      if (cur > 0x7f && len > 3) {
        cur = *p++;
        result |= (cur & 0x7f) << 21;
        if (cur > 0x7f && len > 4) {
          // Note: We don't check to see if cur is out of
          // range here, meaning we tolerate garbage in the
          // high four-order bits.
          cur = *p++;
          result |= cur << 28;
        }
      }
    }
  }
  value = result;
  return p;
}

// Writes LEB128-encoded value at p where p has at least 5 bytes available.
static byte* write_varuint32(byte* p, uint32 value) {
  while (true) {
    byte out = value & 0x7f;
    if (out != value) {
      *p++ = out | 0x80;
      value >>= 7;
    } else {
      *p++ = out;
      break;
    }
  }
  return p;
}

// Writes LEB128-encoded value at p where p has at least 5 bytes available.
static byte* write_varint32(byte* p, int32 value) {
  uint32 extra_bits = uint32(value ^ (value >> 31)) >> 6;
  byte out = value & 0x7f;
  while (extra_bits != 0u) {
    *p++ = out | 0x80;
    value >>= 7;
    out = value & 0x7f;
    extra_bits >>= 7;
  }
  *p++ = out;
  return p;
}

// b must have at least 5 bytes available
static void write_varuint32(Buf& b, uint32 value) {
  b.p = write_varuint32(b.p, value);
}

// b must have at least 5 bytes available
static void write_varint32(Buf& b, int32 value) {
  b.p = write_varint32(b.p, value);
}

// static uint32 sizeof_varuint32(uint32 value) {
//   int len = 0;
//   while (1) {
//     value >>= 7;
//     len++;
//     if (value == 0) {
//       return len;
//     }
//   }
// }

static uint32 sizeof_varint32(int32 value) {
  value = value ^ (value >> 31);
  uint32 x =
    1 /* we need to encode the sign bit */
    + 6
    + 32
    - __builtin_clz(value | 1U);
  // CLZ returns the number of leading 0-bits in x, starting at the most
  // significant bit position. If x is 0, the result is undefined.
  return (x * 37) >> 8;
}

// Writes LEB128-encoded value at p where p has at least 5 bytes available.
// Writes exactly 5 bytes with leading zeros if needed.
static void write_varuint32_fix(byte* p, uint32 value) {
  uint32 max = 0xffffffff;
  while (1) {
    byte out = value & 127;
    value = value >> 7;
    max = max >> 7;
    if (max != 0) {
      out = out | 128;
    }
    *p++ = out;
    if (max == 0) {
      break;
    }
  }
}

// Reserves 5 bytes in b and returns the offset to the varuint32fix
static uint32 alloc_varuint32_fix(Buf& b) {
  uint32 offs = (b.p - b.startp);
  b.p += 5;
  return offs;
}

// Writes a LEB128-encoded unsigned 32-bit integer of fixed length.
// If the value is <=0xffffff leading zeroes are written. e.g.
// write(0xffaa) writes 00 00 aa ff
void VarU32Ptr::write(Buf& b, uint32 v) {
  write_varuint32_fix(b.startp + offs, v);
}

static inline void writeLenPrefixedStr(Buf& b, const byte* str, uint32 len) {
  // Requires len+5 bytes to be available in b
  write_varuint32(b, len);
  writev(b, str, len);
}

// Writes the header for a section using a prefab section byte array
template <size_t N> // assumes v has a nul byte at the end
static inline void beginSection(Buf& b, const byte(&v)[N]) {
  assert(b.sectlen.offs == Future);
  reserve(b, N-1);
  b.sectlen.offs = uint32(b.p - b.startp);
  writev(b, v, N-1);
}

// If there's an open section, this finalizes that section
// and returns the length of it, or 0 if the section was empty or
// if there was no open section.
static inline uint32 endSection(Buf& b) {
  uint32 len = 0;
  if (b.sectlen.offs != Future) {
    len = uint32((b.p - b.startp) - (b.sectlen.offs + 5)); // +5 = varuint32fix
    b.sectlen.write(b, len);
    b.sectlen.offs = Future;
  }
  return len;
}

// Allocates space for and possibly writes a varuint32 to b.
// If v is Future, a varuint32fix is reserved and returned, otherwise
// v is written as a varuint32 to b and a null VarU32Ptr is returned.
static VarU32Ptr writeoralloc_varuint32(Buf& b, uint32 v) {
  reserve(b, 5);
  if (v == Future) {
    return VarU32Ptr{alloc_varuint32_fix(b)};
  } else {
    write_varuint32(b, v);
    return VarU32Ptr();
  }
}

void beginModule(Buf& b) {
  constexpr uint32 Magic   = '\0' | 'a' << 8 | 's' << 16 | 'm' << 24;
  constexpr uint32 Version = 10;
  reserve(b, 8);
  writeu32(b, Magic);
  writeu32(b, Version);
}

void endModule(Buf& b) {
  endSection(b);
}


VarU32Ptr beginSignatures(Buf& b, uint32 count) {
  endSection(b);
  static constexpr byte secthead[] = "\0\0\0\0\0\u000asignatures";
  beginSection(b, secthead);
  return writeoralloc_varuint32(b, count);
}

// signature_entry = param_count return_type param_type*
// param_count     = varuint32  -- the number of parameters to the function.
// return_type     = value_type -- the return type of the function, with 0
//                                 indicating no return type (void).
// param_type      = value_type -- the parameter types of the function.
void writeSignature(Buf& b, Type result, uint32_t nparams, Type* params) {
  reserve(b, 5 + 1 + nparams); // length + result + params
  write_varuint32(b, nparams);
  writeb(b, result);
  uint32_t i = 0;
  while (i != nparams) {
    writeb(b, params[i]);
    i++;
  }
}

VarU32Ptr beginImportTable(Buf& b, uint32 count) {
  endSection(b);
  static constexpr byte secthead[] = "\0\0\0\0\0\u000cimport_table";
  beginSection(b, secthead);
  return writeoralloc_varuint32(b, count);
}

void writeImport(
    Buf&        b,
    uint32      sigIndex,
    const char* modName,
    uint32      modNameLen,
    const char* funName,
    uint32      funNameLen)
{
  // An import is a tuple containing a module name, the name of an exported
  // function to import from the named module, and the signature to use for
  // that import within the importing module. Within a module, the import can
  // be directly called like a function (according to the signature of the
  // import). When the imported module is also WebAssembly, it would be an
  // error if the signature of the import doesn't match the signature of
  // the export.
  //
  // FIELD         TYPE       DESCRIPTION
  // sig_index     varuint32  signature index of the import
  // module_len    varuint32  module string length
  // module_str    bytes      module string of module_len bytes
  // function_len  varuint32  function string length
  // function_str  bytes      function string of function_len bytes
  reserve(b, 5 + 5 + 5 + modNameLen + funNameLen);
  write_varuint32(b, sigIndex);
  writeLenPrefixedStr(b, (const byte*)modName, modNameLen);
  writeLenPrefixedStr(b, (const byte*)funName, funNameLen);
}

static inline void writeIndices(Buf& b, uint32 count, uint32* indices) {
  // FIELD    TYPE        DESCRIPTION
  // count    varuint32   count of indices to follow
  // indices  varuint32*  sequence of indices
  reserve(b, 5 * (count+1));
  write_varuint32(b, count);
  uint32 i = 0;
  while (i != count) {
    write_varuint32(b, indices[i]);
    i++;
  }
}

void writeFunctionTable(Buf& b, uint32 count, uint32* sigIndices) {
  endSection(b);
  static constexpr byte secthead[] = "\0\0\0\0\0\u0013function_signatures";
  beginSection(b, secthead);
  writeIndices(b, count, sigIndices);
}

void writeIndirectFunctionTable(Buf& b, uint32 count, uint32* funIndices) {
  endSection(b);
  static constexpr byte secthead[] = "\0\0\0\0\0\u000efunction_table";
  beginSection(b, secthead);
  writeIndices(b, count, funIndices);
}

void writeMemory(Buf& b, uint32 minPages, uint32 maxPages, bool exported) {
  // The memory section declares the size and characteristics of the memory
  // associated with the module.
  // FIELD          TYPE       DESCRIPTION
  // min_mem_pages  varuint32  minimize memory size in 64KiB pages
  // max_mem_pages  varuint32  maximum memory size in 64KiB pages
  // exported       uint8      1 if the memory is visible outside the module
  endSection(b);

  static constexpr byte secthead[] = "\0\u0006memory";
  reserve(b, strlen_cx(secthead) + 5 + 5 + 1);
  byte* sectp = b.p;
  writev(b, secthead, strlen_cx(secthead));
  write_varuint32(b, minPages);
  write_varuint32(b, maxPages);
  writeb(b, exported ? 1 : 0);

  // ; section memory
  // 0b                 section size = 11
  // 06                 section name len = 6
  // 6d 65 6d 6f 72 79  section name = "memory"
  // 00 01 02 00
  // ; section start_function
  // 11 0e 73

  *sectp = byte(b.p - (sectp + 1)); // +1 = past sectsize varuint32
}

VarU32Ptr beginExportTable(Buf& b, uint32 count) {
  endSection(b);
  static constexpr byte secthead[] = "\0\0\0\0\0\u000cexport_table";
  beginSection(b, secthead);
  return writeoralloc_varuint32(b, count);
}

void writeExport(Buf& b, uint32 funIndex, const char* funName, uint32 funNameLen) {
  // FIELD         TYPE       DESCRIPTION
  // func_index    varuint32  index into the function table
  // function_len  varuint32  function string length
  // function_str  bytes      function string of function_len bytes
  reserve(b, 5 + 5 + funNameLen);
  write_varuint32(b, funIndex);
  writeLenPrefixedStr(b, (const byte*)funName, funNameLen);
}

void writeStartFunction(Buf& b, uint32 funIndex) {
  static constexpr byte secthead[] = "\0\u000estart_function";
  reserve(b, strlen_cx(secthead) + 5);
  byte* sectp = b.p;
  writev(b, (const byte*)secthead, strlen_cx(secthead));
  write_varuint32(b, funIndex);
  *sectp = byte(b.p - (sectp + 1)); // +1 = past sectsize varuint32
}

void beginFunctionBodies(Buf& b, uint32 count) {
  endSection(b);
  static constexpr byte secthead[] = "\0\0\0\0\0\u000ffunction_bodies";
  beginSection(b, secthead);
  write_varuint32(b, count);
}

VarU32Ptr beginFunctionBody(Buf& b, uint32 localCount) {
  // NAME         TYPE          DESCRIPTION
  // body_size    varuint32     size of function body to follow, in bytes
  // local_count  varuint32     number of local entries
  // locals       local_entry*  local variables
  // ast byte*    pre-order     encoded AST
  reserve(b, 5 + 5); // body_size + local_count
  assert(b.bodylen.offs == Future);
  b.bodylen.offs = alloc_varuint32_fix(b);
  return writeoralloc_varuint32(b, localCount);
}

void writeLocal(Buf& b, uint32 count, Type t) {
  assert(t != Type::Void);
  reserve(b, 5 + 1);
  write_varuint32(b, count);
  writeb(b, t);
}

void endFunctionBody(Buf& b) {
  // Called after any AST has been written.
  assert(b.bodylen.offs != Future);
  uint32 bodysize = uint32(b.p - (b.startp + (b.bodylen.offs + 5)));
    //^ +5 body_size itself
  b.bodylen.write(b, bodysize);
  b.bodylen.offs = Future;
}

VarU32Ptr beginDataSegments(Buf& b, uint32 count) {
  endSection(b);
  static constexpr byte secthead[] = "\0\0\0\0\0\u000ddata_segments";
  beginSection(b, secthead);
  return writeoralloc_varuint32(b, count);
}

void writeDataSegment(Buf& b, uint32 offset, const byte* data, uint32 size) {
  // FIELD   TYPE       DESCRIPTION
  // offset  varuint32  the offset in linear memory at which to store the data
  // size    varuint32  size of data (in bytes)
  // data    bytes      sequence of size bytes
  reserve(b, 5 + 5 + size);
  write_varuint32(b, offset);
  write_varuint32(b, size);
  writev(b, data, size);
}

VarU32Ptr beginNames(Buf& b, uint32 count) {
  endSection(b);
  static constexpr byte secthead[] = "\0\0\0\0\0\u0005names";
  beginSection(b, secthead);
  return writeoralloc_varuint32(b, count);
}

VarU32Ptr beginFunctionName(Buf& b, const char* name, uint32 len, uint32 count) {
  // FIELD         TYPE         DESCRIPTION
  // fun_name_len  varuint32    string length, in bytes
  // fun_name_str  bytes        valid utf8 encoding
  // local_count   varuint32    count of local names to follow
  // local_names   local_name*  sequence of local names
  reserve(b, 5 + len);
  write_varuint32(b, len);
  writev(b, (const byte*)name, len);
  return writeoralloc_varuint32(b, count);
}

void writeLocalName(Buf& b, const char* name, uint32 len) {
  reserve(b, 5 + len);
  write_varuint32(b, len);
  writev(b, (const byte*)name, len);
}

// ————————————————————————————————————————————————————————————————————————————

template <typename T, size_t N>
constexpr size_t countof(T const (&)[N]) noexcept { return N; }


void emitSignatures(Buf& b, AstNode& ast) {
  // auto sigcount = beginSignatures(b, Future);
  beginSignatures(b, 2);

  { // func (i64) i32
    Type params[] = {i64};
    writeSignature(b, i32, countof(params), params);
  }
  { // func (i64, i64)
    Type params[] = {i64, i64};
    writeSignature(b, Void, countof(params), params);
  }
  // sigcount.write(2);
}

void emitImportTable(Buf& b, AstNode& ast) {
  auto count = beginImportTable(b);
  writeImport(b, 0, "builtin", strlen("builtin"), "assert", strlen("assert"));
  count.write(b, 1);
}

void emitFunctionTable(Buf& b, AstNode& ast) {
  uint32 sigs[] = {0, 1};
  writeFunctionTable(b, countof(sigs), sigs);
}

void emitIndirectFunctionTable(Buf& b, AstNode& ast) {
  uint32 sigs[] = {0};
  writeIndirectFunctionTable(b, countof(sigs), sigs);
}

void emitExportTable(Buf& b) {
  auto count = beginExportTable(b);
  writeExport(b, 1, "foo", strlen("foo"));
  count.write(b, 1);
}

void emitDataSegments(Buf& b) {
  auto count = beginDataSegments(b);
  writeDataSegment(b, 0, (const byte*)"foo", 3);
  count.write(b, 1);
}

void emitNames(Buf& b) {
  auto funcount = beginNames(b);
  auto loccount = beginFunctionName(b, "fn1", 3);
  writeLocalName(b, "x", 1);
  writeLocalName(b, "y", 1);
  loccount.write(b, 2);
  funcount.write(b, 1);
}

void emitFunctionBodies(Buf& b) {
  beginFunctionBodies(b, 2);

  // func[0]
  // func (arg0 i64) i32 {
  //   x := int32(10)
  //   y := int32(3)
  //   z := int64(7)
  //   return x + y + int32(arg0) + int32(z)
  // }
  auto localcount = beginFunctionBody(b);
  writeLocal(b, 2, Type::i32); // localIndex=0,1
  writeLocal(b, 1, Type::i64); // localIndex=2
  localcount.write(b, 2);
  
  // AST

  // Local variables have value types and are initialized to the appropriate
  // zero value for their type at the beginning of the function, except
  // parameters which are initialized to the values of the arguments passed
  // to the function.

  reserve(b, 1 + 5 + 1 + 5);
  writeb(b, OpSetLocal); // let x = 10
  write_varuint32(b, /*localIndex=*/0);
  writeb(b, OpI32_const);
  write_varint32(b, 10);

  reserve(b, 1 + 5 + 1 + 5);
  writeb(b, OpSetLocal); // let y = 3
  write_varuint32(b, /*localIndex=*/1);
  writeb(b, OpI32_const);
  write_varint32(b, 3);

  reserve(b, 1 + 1 + 5);
  writeb(b, OpReturn); // Nop for void
  writeb(b, OpGetLocal);
  write_varuint32(b, /*localIndex=*/1);

  endFunctionBody(b);

  // func[1]
  beginFunctionBody(b, 0);
  endFunctionBody(b);
}

Err emit_module(Buf& b, AstNode& ast) {
  beginModule(b);

  emitSignatures(b, ast);
  emitImportTable(b, ast);
  emitFunctionTable(b, ast);
  emitIndirectFunctionTable(b, ast);
  writeMemory(b, /*minPages=*/1, /*maxPages=*/2, /*exported=*/false);
  emitExportTable(b);
  writeStartFunction(b, 1);
  emitFunctionBodies(b);
  emitDataSegments(b);
  emitNames(b);

  endModule(b);
  return Err::OK();
}

} // namespace
