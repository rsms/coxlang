#pragma once
#include "slist.h"
#include "istr.h"
#include <string>
#include <forward_list>

// struct Symbol {
//   string name
//   Symbol[] fields
//   Symbol[] methods
//   Symbol[] nestedTypes
// };

// struct Type {
//   Symbol symbol
//   bool isConst
//   Type pointerType = null
// };

// Types used in this actual project
typedef unsigned char       byte;
typedef unsigned int        uint32;
typedef int                 int32;
typedef unsigned long long  uint64;
typedef long long           int64;

// Language type
struct TypeTag {
  byte v;
  bool operator==(const TypeTag& b) const { return b.v == v; }
  bool isComplex() const;
};

// Unresolved unknown type
constexpr TypeTag TyUnresolved{0};

// Simple types
constexpr TypeTag TyBool  {1};
constexpr TypeTag TyI8    {2};
constexpr TypeTag TyU8    {3};
constexpr TypeTag TyI16   {4};
constexpr TypeTag TyU16   {5};
constexpr TypeTag TyI32   {6};
constexpr TypeTag TyU32   {7};
constexpr TypeTag TyI64   {8};
constexpr TypeTag TyU64   {9};
constexpr TypeTag TyF32   {10};
constexpr TypeTag TyF64   {11};
// Implementation-specific simple types
constexpr TypeTag TyUint  {12}; // either 32 or 64 bits
constexpr TypeTag TyInt   {13}; // same size as uint
constexpr TypeTag TyFloat {14}; // same size as uint

// Predeclared types:
//   type byte   u8
//   type char   i32
//   type string []byte

// Complex types
inline bool TypeTag::isComplex() const { return v >= 20; }
constexpr TypeTag TyByteArray{20}; // u=size
constexpr TypeTag TyStruct{21}; // u=fieldCount, children=fieldTypes
constexpr TypeTag TyPointer{22}; // children.first=derefType
constexpr TypeTag TyFunc{23};

struct Type;

struct TypeField {
  // Maybe we should store the AST in here instead?
  // Though if we do, what about imports and their exported types?
  IStr        name;
  const Type* type;
};

struct TypeMethod {
  IStr name;
  // TODO signature
};

// Defines a type and any methods and fields
struct Type {
  TypeTag           tag;
  // TODO:          originMod;
  IStr              name;    // empty for anonymous
  SList<TypeMethod> methods;
  SList<TypeField>  fields;
};

// global constant types
extern const Type TypeUnresolved;
extern const Type TypeBool;
extern const Type TypeI8;
extern const Type TypeU8;
extern const Type TypeI16;
extern const Type TypeU16;
extern const Type TypeI32;
extern const Type TypeU32;
extern const Type TypeI64;
extern const Type TypeU64;
extern const Type TypeF32;
extern const Type TypeF64;
extern const Type TypeUint;
extern const Type TypeInt;
extern const Type TypeFloat;

// Maybe we should move these to module?


// // Type describes any simple or complex type's signature
// struct Type {
//   TypeTag           t;
//   uint32            u;        // interpretation of this value dependends on type
//   SList<const Type> children; // list of children

//   void appendChild(const Type* ctp) { children.append(ctp); }
//   void prependChild(const Type* ctp) { children.prepend(ctp); }

//   // Creates a human-readable representation of the type
//   std::string repr(uint32 depth=0) const;
// };

// // The Types struct provides type interning and allocation.
// // A Module usually owns one Types struct.
// struct Types {
//   // Placeholder type for types that are unresolved
//   static const Type* kUnresolved;

//   // Simple type constants
//   static const Type* kBool;
//   static const Type* kI8;
//   static const Type* kU8;
//   static const Type* kI16;
//   static const Type* kU16;
//   static const Type* kI32;
//   static const Type* kU32;
//   static const Type* kI64;
//   static const Type* kU64;
//   static const Type* kF32;
//   static const Type* kF64;
//   static const Type* kUint;
//   static const Type* kInt;
//   static const Type* kFloat;

//   Type* allocComplex(TypeTag, uint32);
//   const Type* getPointer(const Type*);

//   // free something that was returned from an alloc* function.
//   void freeType(Type*);

//   Types() = default;
//   Types(Types&&) = default;
// private:
//   Types(const Types&) = delete;
//   Type* allocType();

//   std::forward_list<Type*> _freelist;
// };

// inline Type* Types::allocComplex(TypeTag tag, uint32 u) {
//   auto t = allocType();
//   t->t = tag;
//   t->u = u;
//   return t;
// }

// struct FuncType {
//   std::vector<Type> paramTypes;
//   Type              resultType;

//   uint64_t hash() const { return 0; } // TODO
// };
