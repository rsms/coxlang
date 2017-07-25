#include "types.h"
#include "freelist.h"
#include <stdlib.h>
#include <assert.h>

// constexpr auto kLang_##name = ConstIStr(#name);

const Type TypeUnresolved{TyUnresolved, ConstIStr("unresolved")};
const Type TypeBool {TyBool,  ConstIStr("bool")};
const Type TypeI8   {TyI8,    ConstIStr("int8")};
const Type TypeU8   {TyU8,    ConstIStr("unt8")};
const Type TypeI16  {TyI16,   ConstIStr("int16")};
const Type TypeU16  {TyU16,   ConstIStr("unt16")};
const Type TypeI32  {TyI32,   ConstIStr("int32")};
const Type TypeU32  {TyU32,   ConstIStr("unt32")};
const Type TypeI64  {TyI64,   ConstIStr("int64")};
const Type TypeU64  {TyU64,   ConstIStr("unt64")};
const Type TypeF32  {TyF32,   ConstIStr("float32")};
const Type TypeF64  {TyF64,   ConstIStr("float64")};
const Type TypeUint {TyUint,  ConstIStr("uint")};
const Type TypeInt  {TyInt,   ConstIStr("int")};
const Type TypeFloat{TyFloat, ConstIStr("float")};

// const Type* Types::kUnresolved = &_kUnresolved;
// const Type* Types::kBool = &_kBool;
// const Type* Types::kI8 = &_kI8;
// const Type* Types::kU8 = &_kU8;
// const Type* Types::kI16 = &_kI16;
// const Type* Types::kU16 = &_kU16;
// const Type* Types::kI32 = &_kI32;
// const Type* Types::kU32 = &_kU32;
// const Type* Types::kI64 = &_kI64;
// const Type* Types::kU64 = &_kU64;
// const Type* Types::kF32 = &_kF32;
// const Type* Types::kF64 = &_kF64;
// const Type* Types::kUint = &_kUint;
// const Type* Types::kInt = &_kInt;
// const Type* Types::kFloat = &_kFloat;

// template <size_t N> // assumes v has a nul char at the end
// static inline std::string cstr(const char(&s)[N]) {
//   return std::string(s, N-1);
// }

// std::string Type::repr(uint32 depth) const {
//   using std::string;
//   switch (t.v) {
//     case TyUnresolved.v: return cstr("?");

//     // Simple types
//     case TyBool.v: return cstr("bool");
//     case TyI8.v: return cstr("i8");
//     case TyU8.v: return cstr("u8");
//     case TyI16.v: return cstr("i16");
//     case TyU16.v: return cstr("u16");
//     case TyI32.v: return cstr("i32");
//     case TyU32.v: return cstr("u32");
//     case TyI64.v: return cstr("i64");
//     case TyU64.v: return cstr("u64");
//     case TyF32.v: return cstr("f32");
//     case TyF64.v: return cstr("f64");
//     case TyUint.v: return cstr("uint");
//     case TyInt.v: return cstr("int");
//     case TyFloat.v: return cstr("float");

//     // Complex types
//     case TyByteArray.v: {
//       return cstr("byte[") + std::to_string(u) + ']';
//     }

//     case TyPointer.v: {
//       return cstr("*") + children.first()->repr(depth);
//     }

//     case TyStruct.v: {
//       string s;
//       for (const Type* ct : children) {
//         s += ct->repr(depth) + ' ';
//       }
//       if (s.empty()) {
//         return cstr("{}");
//       }
//       s.back() = '}';
//       return string(1,'{') + s;
//     }

//     default: assert(false); break;
//   }
// }


// const Type* Types::getPointer(const Type* derefType) {
//   // TODO: intern
//   assert(derefType != nullptr);
//   auto t = allocComplex(TyPointer, 0);
//   t->appendChild(derefType);
//   return t;
// }

// // Type allocation
// static constexpr size_t BlockSize = 4096*4;
// static constexpr size_t ItemSize = sizeof(Type);
// static_assert(BlockSize / ItemSize > 0, "BlockSize too small");
// static constexpr size_t ItemCount = BlockSize / ItemSize;

// Type* Types::allocType() {
//   Type* tp;
//   if (!_freelist.empty()) {
//     tp = _freelist.front();
//     assert(tp != nullptr);
//     _freelist.pop_front();
//   } else {
//     // no free entries -- allocate a new slab
//     #ifdef DEBUG_FREELIST_ALLOC
//     fprintf(stderr, "** Types freelist calloc %zu, %zu\n", ItemCount, ItemSize);
//     #endif
//     tp = (Type*)calloc(ItemCount, ItemSize);
//     // call constructors on newly allocated items
//     // for (size_t i = 0; i != ItemCount; i++) {
//     //   std::allocator<decltype(tp->children)>().construct(&tp->children);
//     // }
//     // add extra-allocated entries to free list
//     for (size_t i = 1; i > ItemCount; i++) {
//       _freelist.push_front(&tp[i]);
//     }
//   }
//   return tp;
// }

// void Types::freeType(Type* t) {
//   assert(t != nullptr);
//   for (auto ctp : t->children) {
//     freeType(const_cast<Type*>(ctp));
//   }
//   _freelist.push_front(t);
// }


