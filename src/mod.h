#pragma once
#include "istr.h"
#include "ast.h"
#include "error.h"
#include "hash.h"
#include "types.h"
#include <set>
#include <vector>

// Module represents a package module and might contain information parsed
// from several translation units.
// This struct closely maps to the information required to generate target code.
struct Module {
  IStr  name;  // name of package
  Types types;

  // Returns the type for an Ident node that is interpreted as a typename.
  // Returns kUnresolved if type is not resolveable.
  // const Type* typeofTypename(const AstNode&);

  // If the node's type is kUnresolved, it's registered as needing resolution.
  void regUnresolvedType(const AstNode&);

  // addNamed returns an existing node if there's already something defined
  // in this module with the same name.
  // Otherwise null is returned and n is associated with name.
  AstNode* addNamed(const IStr& name, AstNode& n);
  AstNode* findNamed(const IStr& name); // null if not found

  Type* addType(const IStr& name);
  const Type* findType(const IStr& name);

  // Err addFunc(AstNode& n);

  Module()
    : _freeTypep{_initFreeTypes}
    , _nfreeType{sizeof(_initFreeTypes)/sizeof(*_initFreeTypes)}
  {}

private:
  using IStrNodeMap = IStr::Map<AstNode*>;

  // Module-level identifiers
  IStrNodeMap _idents;    // all module-level identifiers
  // IStrNodeMap _consts; // const name ...
  // IStrNodeMap _vars;   // var name ...
  // IStrNodeMap _funcs;  // func name ...

  IStr::Map<TypeDef*> _typedefs;  // type name ...

  // free list of types
  Type*  _freeTypep;
  uint32 _nfreeType;
  Type   _initFreeTypes[8]; // _freeTypep initially points to this
};

// Register n as unresolved in the module, but only if its type is unknown.
inline void Module::regUnresolvedType(const AstNode& n) {
  if (n.ty == types.kUnresolved) {
    // TODO: add to list or set of unresolved nodes
  }
}

