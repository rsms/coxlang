#include "wasm.h"

static const uint32 kMagic   = '\0' | 'a' << 8 | 's' << 16 | 'm' << 24;
static const uint32 kVersion = 10;

// Writes a LEB128-encoded unsigned 32-bit integer
static void write_varuint32(WCode& c, uint32 value) {
  while (1) {
    // b = low order 7 bits of value
    auto b = value & 127;
    value = value >> 7;
    if (value != 0) {
      // set high order bit of byte
      b = b | 128;
    }
    c.writec(char(b));
    if (value == 0) {
      break;
    }
  }
}

// Writes a LEB128-encoded unsigned 32-bit integer at pch.
// When used for patching, assumes existing is all zeros.
// Note: WASM spec says "varuint32 values may contain leading zeros"
static char* write_varuint32(char* pch, uint32 value) {
  while (1) {
    auto b = value & 127;
    value = value >> 7;
    if (value != 0) {
      b = b | 128;
    }
    *pch = char(b);
    pch++;
    if (value == 0) {
      break;
    }
  }
  return pch;
}

static void write_lenstr(WCode& c, const char* p, uint32 len) {
  write_varuint32(c, len);
  c.writev(p, len);
}

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
static constexpr byte kSection_signatures[] = {
  0,0,0,0,0,  10,  's','i','g','n','a','t','u','r','e','s'
};


// Returns offset in c to the beginning of this section
// inline static uint32 gen_section(WCode& c, const char* ID) {
//   auto offs = c.len;
//   c.reserve(4);
//   c.writeu32(0); // we fill this in later
//   c.reserve(4+len);
//   write_lenstr(ID, strlen(ID)); // section identifier string length & bytes
//   return offs;
// }

// Returns section size
template <size_t N>
static inline uint32_t write_section(WCode& c, const char(&v)[N]) {
  c.reserve(N);
  c.writev(v, N);
  return N + 5; // +5=sectsize varuint32fix
}


static void gen_signatures(WCode& c, AstNode& prog) {
  // The signatures section declares all function signatures that will be
  // used in the module.
  auto sectoffs = c.len;
  uint32_t sectsize = write_section(c, kSection_signatures);

  // ...

  // update section size
  write_varuint32(c.p + sectoffs, sectsize);
}

Err wasm_genmod(WCode& c, AstNode& prog) {

  // Module preamble
  c.reserve(8);
  c.writeu32(kMagic);
  c.writeu32(kVersion);

  // This preamble is followed by a sequence of sections
  gen_signatures(c, prog);

  // emit_signatures(m, prog);
  // emit_importTable(m, prog);
  // emit_functionSignatures(m, prog);
  // emit_indirectFunctionTable
  // emit_memory(m, prog);
  // emit_exportTable(m, prog);
  // emit_startFunction
  // emit_functionBodies(m, prog);
  // emit_dataSegments(m, prog);
  // emit_Names
  return Err::OK();
}

// —————————————————————————————————————————————————————————————————————————

namespace wasm {

static inline void reserve(Buf& b, uint32 nbytes) {
  if (b.cap - b.len < nbytes) {
    b.cap += 4096;
    b.p = (char*)realloc(b.p, b.cap);
  }
}

static inline void writec(Buf& b, char c) {
  b.p[b.len++] = c;
}

static inline void writeu32(Buf& b, uint32 u) {
  *((uint32*)(b.p + b.len)) = u;
  b.len += 4;
}

static inline void writev(Buf& b, const char* pch, uint32 len) {
  memcpy((void*)(b.p + b.len), (const void*)pch, len);
  b.len += len;
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

static void write_varuint32(Buf& b, uint32 value) {
  auto p = b.p + b.len;
  b.len += write_varuint32(p, value) - p;
}

static uint32 sizeof_varuint32(uint32 value) {
  int len = 0;
  while (1) {
    value >>= 7;
    len++;
    if (value == 0) {
      return len;
    }
  }
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

// Reserves 5 bytes in b and returns a varuint32 pointer
static VarUInt32 write_varuint32_fix(Buf& b) {
  VarUInt32 v{b.p + b.len};
  b.len += 5;
  return v;
}

// Writes a LEB128-encoded unsigned 32-bit integer of fixed length.
// If the value is <=0xffffff leading zeroes are written. e.g.
// write(0xffaa) writes 00 00 aa ff
void VarUInt32::write(uint32 v) {
  write_varuint32_fix(p, v);
}

// Writes the header for a section using a prefab section byte array
template <size_t N>
static inline void beginSection(Buf& b, const byte(&v)[N]) {
  assert(b.sectlen.p == nullptr);
  b.sectlen.p = b.p + b.len;
  reserve(b, N);
  writev(b, v, N);
}

// If there's an open section, this finalizes that section
// and returns the length of it, or 0 if the section was empty or
// if there was no open section.
static inline uint32 endSection(Buf& b) {
  uint32 len = 0;
  if (b.sectlen.p != nullptr) {
    uint32 sectbegin = b.sectlen.p - 5; // -5 = varuint32fix;
    uint32 sectend = b.p + b.len;
    len = sectend - sectbegin;
    b.sectlen.write(len);
    b.sectlen.p = nullptr;
  }
  return len;
}

void beginModule(Buf& b) {
  reserve(b, 8);
  writeu32(b, kMagic);
  writeu32(b, kVersion);
}

void endModule(Buf& b) {
  endSection(b);
}


void beginSignatures(Buf& b, uint32 count) {
  endSection(b);
  b.sectoffs = b.len;
  beginSection(b, kSection_signatures);

  // write count of signature entries to follow
  reserve(b, 4);
  if (count == 0) {
    return write_varuint32_fix(b);
  }
  write_varuint32(b, count);
  return VarUInt32{};
}


VarUInt32 beginSignaturesVar(Buf& b) {
  if (b.sectoffs != 0) {
    endsect(b, b.sectoffs);
  }
  b.sectoffs = b.len;
  writesect(b, kSection_signatures);

  // write count of signature entries to follow
  return initVarUInt32(b);
}


// signature_entry = param_count return_type param_type*
// param_count     = varuint32  -- the number of parameters to the function.
// return_type     = value_type -- the return type of the function, with 0
//                                 indicating no return type (void).
// param_type      = value_type -- the parameter types of the function.
void writeSignature(Buf& b, Type result, uint32_t nparams, Type* params) {
  reserve(b, 4 + 1 + nparams); // length + result + params
  write_varuint32(b, nparams);
  writec(b, result);
  uint32_t i = 0;
  while (i != nparams) {
    writec(b, params[i]);
    i++;
  }
}

// ————————————————————————————————————————————————————————————————————————————

Err emit_signatures(Buf& b, AstNode& ast) {
  auto count = beginSignatures(b);

  // func (i64) i32
  sigs.write(i32, 1, &i64);

  // func (i64, i64)
  Type params[] = {i64, i64};
  sigs.write(Void, sizeof(params)/sizeof(Type), params);
  
  count.set(2);
}

Err emit_module(Buf& b, AstNode& ast) {
  beginModule(b);
  emit_signatures(b, ast);
  // emit_importTable(m, ast);
  // emit_functionSignatures(m, ast);
  // emit_indirectFunctionTable(m, ast);
  // emit_memory(m, ast);
  // emit_exportTable(m, ast);
  // emit_startFunction(m, ast);
  // emit_functionBodies(m, ast);
  // emit_dataSegments(m, ast);
  // emit_names(m, ast);
  endModule(b);
  return Err::OK();
}

} // namespace
