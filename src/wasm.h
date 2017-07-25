#pragma once
#include "error.h"
#include "ast.h"
#include "types.h"

namespace wasm {

// Value type
enum Type : unsigned char {
  Void = 0,
  i32 = 1,
  i64 = 2,
  f32 = 3,
  f64 = 4,
};

// Opcodes
enum OpCode : byte {
// Control flow operators
// NAME   CODE // IMMEDIATE           DESCRIPTION
OpNop    =0x00,//                     no operation
OpBlock  =0x01,// count varuint32     a sequence of expressions, the last of
               //                     which yields a value.
OpLoop   =0x02,// count varuint32     a block which can also form control flow
               //                     loops.
OpIf     =0x03,//                     high-level one-armed if
OpIfElse =0x04,//                     high-level two-armed if
OpSelect =0x05,//                     select one of two values based on condition
OpBr     =0x06,// reldepth varuint32  break that targets a outer nested block.
OpBrIf   =0x07,// reldepth varuint32  conditional break that targets a outer
               //                     nested block.
OpBrTable=0x08,//                     see branch table control flow
OpReturn =0x14,//                     return zero or one value from this function
OpUnreachable =0x15,//                trap immediately

// Basic operators
// NAME         CODE  // IMMEDIATE             DESCRIPTION
OpI32_const    =0x0a, // value varint32        a constant value interpreted as i32
OpI64_const    =0x0b, // value varint64        a constant value interpreted as i64
OpF64_const    =0x0c, // value uint64          a constant value interpreted as f64
OpF32_const    =0x0d, // value uint32          a constant value interpreted as f32
OpGetLocal     =0x0e, // localIndex varuint32  read a local variable or parameter
OpSetLocal     =0x0f, // localIndex varuint32  write a local variable or parameter
OpCall         =0x12, // funIndex varuint32    call a function by its index
OpCallIndirect =0x13, // sigIndex varuint32    call a function indirect with an
                      //                       expected signature.
OpCallImport   =0x1f, // impIndex varuint32    call an imported function by its
                      //                       index.

// Memory loads
OpI32_load8_s  =0x20, // memory_immediate
OpI32_load8_u  =0x21, // memory_immediate
OpI32_load16_s =0x22, // memory_immediate
OpI32_load16_u =0x23, // memory_immediate
OpI64_load8_s  =0x24, // memory_immediate
OpI64_load8_u  =0x25, // memory_immediate
OpI64_load16_s =0x26, // memory_immediate
OpI64_load16_u =0x27, // memory_immediate
OpI64_load32_s =0x28, // memory_immediate
OpI64_load32_u =0x29, // memory_immediate
OpI32_load     =0x2a, // memory_immediate
OpI64_load     =0x2b, // memory_immediate
OpF32_load     =0x2c, // memory_immediate
OpF64_load     =0x2d, // memory_immediate

// Memory stores
OpI32_store8   =0x2e, // memory_immediate
OpI32_store16  =0x2f, // memory_immediate
OpI64_store8   =0x30, // memory_immediate
OpI64_store16  =0x31, // memory_immediate
OpI64_store32  =0x32, // memory_immediate
OpI32_store    =0x33, // memory_immediate
OpI64_store    =0x34, // memory_immediate
OpF32_store    =0x35, // memory_immediate
OpF64_store    =0x36, // memory_immediate

// Memory misc (no immediates)
OpMemorySize  =0x3b,
OpGrowMemory  =0x39,

// Simple operators (no immediates)
OpI32_add =0x40,
OpI32_sub =0x41,
OpI32_mul =0x42,
OpI32_div_s =0x43,
OpI32_div_u =0x44,
OpI32_rem_s =0x45,
OpI32_rem_u =0x46,
OpI32_and =0x47,
OpI32_or  =0x48,
OpI32_xor =0x49,
OpI32_shl =0x4a,
OpI32_shr_u =0x4b,
OpI32_shr_s =0x4c,
OpI32_rotr  =0xb6,
OpI32_rotl  =0xb7,
OpI32_eq  =0x4d,
OpI32_ne  =0x4e,
OpI32_lt_s  =0x4f,
OpI32_le_s  =0x50,
OpI32_lt_u  =0x51,
OpI32_le_u  =0x52,
OpI32_gt_s  =0x53,
OpI32_ge_s  =0x54,
OpI32_gt_u  =0x55,
OpI32_ge_u  =0x56,
OpI32_clz =0x57,
OpI32_ctz =0x58,
OpI32_popcnt  =0x59,
OpI32_eqz =0x5a,
OpI64_add =0x5b,
OpI64_sub =0x5c,
OpI64_mul =0x5d,
OpI64_div_s =0x5e,
OpI64_div_u =0x5f,
OpI64_rem_s =0x60,
OpI64_rem_u =0x61,
OpI64_and =0x62,
OpI64_or  =0x63,
OpI64_xor =0x64,
OpI64_shl =0x65,
OpI64_shr_u =0x66,
OpI64_shr_s =0x67,
OpI64_rotr  =0xb8,
OpI64_rotl  =0xb9,
OpI64_eq  =0x68,
OpI64_ne  =0x69,
OpI64_lt_s  =0x6a,
OpI64_le_s  =0x6b,
OpI64_lt_u  =0x6c,
OpI64_le_u  =0x6d,
OpI64_gt_s  =0x6e,
OpI64_ge_s  =0x6f,
OpI64_gt_u  =0x70,
OpI64_ge_u  =0x71,
OpI64_clz =0x72,
OpI64_ctz =0x73,
OpI64_popcnt  =0x74,
OpI64_eqz =0xba,
OpF32_add =0x75,
OpF32_sub =0x76,
OpF32_mul =0x77,
OpF32_div =0x78,
OpF32_min =0x79,
OpF32_max =0x7a,
OpF32_abs =0x7b,
OpF32_neg =0x7c,
OpF32_copysign  =0x7d,
OpF32_ceil  =0x7e,
OpF32_floor =0x7f,
OpF32_trunc =0x80,
OpF32_nearest =0x81,
OpF32_sqrt  =0x82,
OpF32_eq  =0x83,
OpF32_ne  =0x84,
OpF32_lt  =0x85,
OpF32_le  =0x86,
OpF32_gt  =0x87,
OpF32_ge  =0x88,
OpF64_add =0x89,
OpF64_sub =0x8a,
OpF64_mul =0x8b,
OpF64_div =0x8c,
OpF64_min =0x8d,
OpF64_max =0x8e,
OpF64_abs =0x8f,
OpF64_neg =0x90,
OpF64_copysign  =0x91,
OpF64_ceil  =0x92,
OpF64_floor =0x93,
OpF64_trunc =0x94,
OpF64_nearest =0x95,
OpF64_sqrt  =0x96,
OpF64_eq  =0x97,
OpF64_ne  =0x98,
OpF64_lt  =0x99,
OpF64_le  =0x9a,
OpF64_gt  =0x9b,
OpF64_ge  =0x9c,
OpI32_trunc_s_f32 =0x9d,
OpI32_trunc_s_f64 =0x9e,
OpI32_trunc_u_f32 =0x9f,
OpI32_trunc_u_f64 =0xa0,
OpI32_wrap_i64  =0xa1,
OpI64_trunc_s_f32 =0xa2,
OpI64_trunc_s_f64 =0xa3,
OpI64_trunc_u_f32 =0xa4,
OpI64_trunc_u_f64 =0xa5,
OpI64_extend_s_i32  =0xa6,
OpI64_extend_u_i32  =0xa7,
OpF32_convert_s_i32 =0xa8,
OpF32_convert_u_i32 =0xa9,
OpF32_convert_s_i64 =0xaa,
OpF32_convert_u_i64 =0xab,
OpF32_demote_f64  =0xac,
OpF32_reinterpret_i32 =0xad,
OpF64_convert_s_i32 =0xae,
OpF64_convert_u_i32 =0xaf,
OpF64_convert_s_i64 =0xb0,
OpF64_convert_u_i64 =0xb1,
OpF64_promote_f32 =0xb2,
OpF64_reinterpret_i64 =0xb3,
OpI32_reinterpret_f32 =0xb4,
OpI64_reinterpret_f64 =0xb5,

};

// WASM code buffer. (Declared later in this file.)
struct Buf;

// Future is a constant for signaling that a number should be allocated
// but will be written in the future.
static constexpr uint32 Future = ~0;

// Pointer for later rewriting/patching of a LEB128-encoded varint.
struct VarU32Ptr {
  uint32 offs = Future;     // offset of allocated number into buffer
  void write(Buf&, uint32); // write value at Buf.startp+offs
};

// The following functions should be called in order as they appear here.
// Some functions are optional whilst other are mandatory.
// To learn more, please refer to the WebAssembly standard documentation.
//
// Functions returning a varint pointer accepts Future in place of count,
// in which case the value of count is expected to be written in the future
// by calling write() on the returned varint pointer.

// Writes the header for a module. Should be balanced by a call to endModule.
void beginModule(Buf&);

// The signatures section declares all function signatures that will be used
// in the module. The effective signature index is the sequence in which the
// writeSignature function is called. First call writes to index 0,
// second call to index 1, and so on.
VarU32Ptr beginSignatures(Buf&, uint32 count=Future);
void writeSignature(Buf&, Type result, uint32 nparams, Type* params);

// The import section declares all imports that will be used in the module.
// signatureIndex should point to the index into the signatures section that
// describes the function signature of the imported function.
VarU32Ptr beginImportTable(Buf&, uint32 count=Future);
void writeImport(Buf&,
  uint32      sigIndex,    // index into the Signatures table
  const char* modName,     // module string
  uint32      modNameLen,  // module string length
  const char* funName,     // function string
  uint32      funNameLen); // function string length

// The Function Table section defines a table of all functions
// declared in this module (not including imported functions), giving each
// function an index (the order in sigIndices) mapping to an index into the
// signatures section. e.g.
// sigIndices=[1, 1, 3] creates mappings:
//   0 => signature 1
//   1 => signature 1
//   2 => signature 3
// The count of the Function Table and Function Bodies must be the same and
// the ith signature corresponds to the ith function body.
void writeFunctionTable(Buf&, uint32 count, uint32* sigIndices);

// The indirect function table section defines the module's indirect functions.
// Indirect calls allow calling functions that are unknown at compile time.
// funIndices refers to indices into the Function Table.
void writeIndirectFunctionTable(Buf&, uint32 count, uint32* funIndices);

// The memory section declares the size and characteristics of the memory associated
// with the module.
// minPages -- minimum memory size in 64KiB pages
// maxPages -- maximum memory size in 64KiB pages
// exported -- true if the memory is visible outside the module
void writeMemory(Buf&, uint32 minPages, uint32 maxPages, bool exported);

// The export table section declares all exports from the module.
VarU32Ptr beginExportTable(Buf&, uint32 count=Future);
void writeExport(Buf&,
  uint32      funIndex,    // index into the Function Table
  const char* funName,     // function name string
  uint32      funNameLen); // function name string length

// The start function section declares the start function.
// funIndex points into the Function Table.
// Note that the start function takes no parameters and returns no value.
void writeStartFunction(Buf&, uint32 funIndex);

// The Function Bodies section assigns a body to every function in the module.
// The count of Function Table and Function Bodies must be the same and
// the ith signature corresponds to the ith function body.
// Because of this restriction, this function requires an "up-front" count,
// rather than returning a VarU32Ptr.
void beginFunctionBodies(Buf&, uint32 count);

// Function bodies consist of a sequence of local variable declarations
// followed by a dense pre-order encoding of an Abstract Syntax Tree.
// Each node in the abstract syntax tree corresponds to an operator,
// such as `i32.add` or `if` or `block`. Operators are encoding by an opcode byte
// followed by immediate bytes (if any), followed by children nodes (if any).
VarU32Ptr beginFunctionBody(Buf&, uint32 localCount=Future);
// Each local entry declares a number of local variables of a given type.
// It is legal to have several entries with the same type.
// count signifies the number of local variables of the following type.
void writeLocal(Buf&, uint32 count, Type);
// At this point you should write the AST describing the function body,
// and then finally call endFunctionBody.
void endFunctionBody(Buf&);

// The data segments section declares the initialized data that should be
// loaded into the linear memory.
VarU32Ptr beginDataSegments(Buf&, uint32 count=Future);
void writeDataSegment(Buf&,
  uint32      offset, // the offset in linear memory at which to store the data
  const byte* data,   // sequence of bytes
  uint32      size);  // size of data in bytes

// The names section does not change execution semantics and a validation
// error in this section does not cause validation for the whole module to fail
// and is instead treated as if the section was absent. The expectation is that,
// when a binary WASM module is viewed in a browser or other
// development environment, the names in this section will be used as the names
// of functions and locals in the text format.
VarU32Ptr beginNames(Buf&, uint32 count=Future);
// The sequence of FunctionName assigns names to the corresponding function index.
// The count may be greater or less than the actual number of functions.
VarU32Ptr beginFunctionName(Buf&,
  const char* funName,            // UTF8-encoded text
  uint32      funNameLen,         // length of funName in bytes
  uint32      localCount=Future); // count of local names to follow
// The sequence of LocalName assigns names to the corresponding local index.
// The count may be greater or less than the actual number of locals.
// Name must be UTF8-encoded text.
void writeLocalName(Buf&, const char* name, uint32 nameLen);

// Finalize the module
void endModule(Buf&);

// WASM code buffer
struct Buf {
  // Number of bytes a buffer grows by.
  // The capacity of a buffer is thus always a multiple of this value.
  static constexpr auto GrowSize = 512;

  byte*     startp = nullptr; // start of memory
  byte*     endp = nullptr;   // end of memory
  byte*     p = nullptr;      // next byte write position
  VarU32Ptr sectlen;          // section length varint pointer
  VarU32Ptr bodylen;          // function body length varint pointer

  size_t size() const {
    return size_t(p - startp);
  }
  
  byte* data() {
    return startp;
  }
  
  void clear() {
    p = startp;
    sectlen.offs = Future;
    bodylen.offs = Future;
  }
};




Err emit_module(Buf&, AstNode&);


} // namespace wasm
