#include "mod.h"

// Imported modules only affect the space in which they are imported in:
// 
// package foo
// type Foo int
// func Foo.double() int { return @ * 2; }
// // Foo.drool() is always undefined in here
// // Foo.lives() is always undefined in here
//
// package cat
// import foo
// func foo.Foo.lives() int { return 7; }
// // foo.Foo.drool() is always undefined in here
//
// package dog
// import foo
// func foo.Foo.drool() {}
// // foo.Foo.lives() is always undefined in here
//
// package bar
// import foo
// import cat
// import dog
// func Foo.manyLives() int { @drool(); return @.lives() * @.double(); }
//
// When resolving a method for a call, we first search the local module, then
// imported modules. That way there will be no accidental "spill" of methods
// across sibling modules, i.e. cat and dog does not affect each other.
//
// This means that methods added to imported types must be declared in the
// module where the method declaration is, not the type declaration. So we will
// need some kind of "type that is an extension to a type in a different module."
//
// Another way of thinking about it, or implement this, is that when a module
// is imported, we copy all types from that module into the importing module.
// This is probably a bad idea though, because dynamically importing modules
// would be complicated and slow (we'd need to rebuild the types at runtime.)
//

#define assertAstType(np, T) do { \
  assert((np) != nullptr); \
  assert((np)->type == Ast##T); \
} while (0)

// Type for an identifier that is used as a typename
// const Type* Module::typeofTypename(const AstNode& n) {
//   // TODO: look up name in module -- type might be defined already.
//   if (n.type == AstQualIdent) {
//     // TODO
//     return types.kUnresolved;
//   }
//   assert(n.type == AstIdent);
//   switch (n.value.str.hash()) {
//     case IStr::hash("bool"):    return types.kBool;
//     case IStr::hash("int8"):    return types.kI8;
//     case IStr::hash("uint8"):   return types.kU8;
//     case IStr::hash("int16"):   return types.kI16;
//     case IStr::hash("uint16"):  return types.kU16;
//     case IStr::hash("int32"):   return types.kI32;
//     case IStr::hash("uint32"):  return types.kU32;
//     case IStr::hash("int64"):   return types.kI64;
//     case IStr::hash("uint64"):  return types.kU64;
//     case IStr::hash("float32"): return types.kF32;
//     case IStr::hash("float64"): return types.kF64;
//     case IStr::hash("uint"):    return types.kUint;
//     case IStr::hash("int"):     return types.kInt;
//     case IStr::hash("float"):   return types.kFloat;
//     default: {
//       // Try to resolve type by name
//       auto tdef = findTypeDef(n.value.str);
//       return tdef == nullptr ? types.kUnresolved : tdef->ty;
//     }
//   }
// }


AstNode* Module::addNamed(const IStr& name, AstNode& n) {
  // TODO: decide if builtin types can be replaced or not
  // switch (name.hash()) {
  //   case IStr::hash("bool"):    return &Type::Bool;
  //   case IStr::hash("int8"):    return &Type::I8;
  //   case IStr::hash("uint8"):   return &Type::U8;
  //   case IStr::hash("int16"):   return &Type::I16;
  //   case IStr::hash("uint16"):  return &Type::U16;
  //   case IStr::hash("int32"):   return &Type::I32;
  //   case IStr::hash("uint32"):  return &Type::U32;
  //   case IStr::hash("int64"):   return &Type::I64;
  //   case IStr::hash("uint64"):  return &Type::U64;
  //   case IStr::hash("float32"): return &Type::F32;
  //   case IStr::hash("float64"): return &Type::F64;
  //   case IStr::hash("uint"):    return &Type::Uint;
  //   case IStr::hash("int"):     return &Type::Int;
  //   case IStr::hash("float"):   return &Type::Float;
  //   default: {
  auto I = _idents.emplace(std::pair<IStr,AstNode*>{name, &n});
  return I.second ? nullptr : I.first->first;
}


AstNode* Module::findNamed(const IStr& name) {
  switch (n.value.str.hash()) {
    // TODO: Builtin types
    // case IStr::hash("bool"):    return &Type::kBool;
    // case IStr::hash("int8"):    return &Type::kI8;
    // case IStr::hash("uint8"):   return &Type::kU8;
    // case IStr::hash("int16"):   return &Type::kI16;
    // case IStr::hash("uint16"):  return &Type::kU16;
    // case IStr::hash("int32"):   return &Type::kI32;
    // case IStr::hash("uint32"):  return &Type::kU32;
    // case IStr::hash("int64"):   return &Type::kI64;
    // case IStr::hash("uint64"):  return &Type::kU64;
    // case IStr::hash("float32"): return &Type::kF32;
    // case IStr::hash("float64"): return &Type::kF64;
    // case IStr::hash("uint"):    return &Type::kUint;
    // case IStr::hash("int"):     return &Type::kInt;
    // case IStr::hash("float"):   return &Type::kFloat;
    default: {
      auto I = _idents.find(name);
      return (I == _idents.end()) ? nullptr : I->second;
    }
  }
}


TypeDef* Module::addType(const IStr& name) {
  Type* ty;
  if (_nfreeType != 0) {
    // Take one from free-list
    ty = _freeTypep;
    _freeTypep++;
    _nfreeType--;
  } else {
    // Allocate some more
    constexpr size_t Count = 8;
    ty = (Type*)calloc(Count, sizeof(Type));
    _freeTypep = ty + 1;
    _nfreeType = Count - 1;
  }

  _typedefs.emplace(decltype(_typedefs)::value_type{name, ty});
  return ty;
}


const Type* Module::findType(const IStr& name) {
  switch (name.hash()) {
    case IStr::hash("bool"):    return &Type::Bool;
    case IStr::hash("int8"):    return &Type::I8;
    case IStr::hash("uint8"):   return &Type::U8;
    case IStr::hash("int16"):   return &Type::I16;
    case IStr::hash("uint16"):  return &Type::U16;
    case IStr::hash("int32"):   return &Type::I32;
    case IStr::hash("uint32"):  return &Type::U32;
    case IStr::hash("int64"):   return &Type::I64;
    case IStr::hash("uint64"):  return &Type::U64;
    case IStr::hash("float32"): return &Type::F32;
    case IStr::hash("float64"): return &Type::F64;
    case IStr::hash("uint"):    return &Type::Uint;
    case IStr::hash("int"):     return &Type::Int;
    case IStr::hash("float"):   return &Type::Float;
    default: {
      auto I = _typedefs.find(name);
      return (I == _typedefs.end()) ? nullptr : I->second;
    }
  }
}


// Err Module::addFunc(AstNode& n) {
//   assert(n.type == AstFuncDecl || n.type == AstMethodDecl);

//   // What to do with AstMethodDecl?
//   // Maybe we can derive a IStr identifier for a type and combine
//   // that with the name of the method?
//   //
//   // Or maybe we actually resolve the type of the receiver and add
//   // it to some table of types and just refer to the identifier in
//   // that table?

//   auto hasBody = [&](AstNode& n) {
//     return !n.children.empty() && n.children.last->type == AstBlock;
//   };

//   if (n.type == AstFuncDecl) {
//     auto I = _idents.emplace(std::pair<IStr,AstNode*>{n.value.str, &n});
//     if (!I.second) {
//       if (!hasBody(n)) {
//         // n is just a signature -- ignore collision
//       } else {
//         auto& n2 = *I.first->second;
//         if (n2.type == AstFuncDecl && !hasBody(n2)) {
//           // existing is just a signture -- replace
//           I.first->second = &n;
//         } else {
//           return Err(0, "duplicate name \"", n.value.str, "\"");
//         }
//       }
//     }
//   }

//   return Err::OK();
// }
