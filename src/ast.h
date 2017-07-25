#pragma once
#include "srcloc.h"
#include "text.h"
#include "istr.h"
#include "types.h"
#include "slist.h"
#include <string>

// Type of AST nodes.
// For each node, theres a AstType enum constant defined,
// e.g. for Ident its `AstIdent`.
#define RX_AST_NODES(_) \
  /* Name */ \
  _( Program ) \
  _( Bool ) \
  _( IntConst ) \
  _( DataTail ) \
  _( String ) \
  _( RawString ) \
  _( Ident ) \
  _( QualIdent ) \
  _( ConstDecl ) \
  _( ConstSpec ) \
  _( TypeDecl ) \
  _( TypeSpec ) \
  _( FieldDecl ) \
  _( StructType ) \
  _( PointerType ) \
  _( FuncDecl ) \
  _( MethodDecl ) \
  _( FuncSig ) \
  _( ParamDecl ) \
  _( Block ) \
  _( UnaryOp )  /*  op = value.i  */ \

#define RX_AST_NODES_DEFINED

enum AstType {
  AstNone = 0,
  #define M(Name) Ast##Name,
  RX_AST_NODES(M)
  #undef M
};

const std::string& ast_typename(AstType);

// package declaration at top of source files
struct AstPkgDecl {
  IStr   name;    // name of the package (an Identifier)
  Text   doc;     // any comment written directly above `package`
  SrcLoc srcloc;  // location in source
};

// Represents a node in a tree
struct AstNode {
  AstType  type;   // type of node
  SrcLoc   loc;    // location in source

  // value
  union {
    IStr     str;
    uint64_t i;
    double   f;
  } value;

  const Type*        ty = nullptr; // DEPRECATED in favor for typeDef
  TypeDef*           typeDef = nullptr;

  AstNode*           nextSib = nullptr; // Sibling link (children and free-list)
  SListIntr<AstNode> children; // Children list

  void appendChild(AstNode& cnp) { children.append(cnp); }
  void prependChild(AstNode& cnp) { children.prepend(cnp); }
  void appendChildList(AstNode& first) { children.appendList(first); }
};


// Appends readable representation of AstNode `n` to `os`. Returns `os`.
std::ostream& ast_repr(AstNode& n, std::ostream& os, uint32_t depth=0);


// Node allocator
struct AstAllocator {
  // Allocate a node. Null is returned only when ENOMEM.
  AstNode* alloc();

  // Free a node previously allocated with this allocator.
  void free(AstNode*);

  // Convenience helpers to alloc & set type.
  // E.g. allocIdent() => n.type==AstNode::Ident
  // #define M(Name) \
  // AstNode* alloc##Name() { \
  //   auto n = alloc(); \
  //   n->type = Ast##Name; \
  //   return n; \
  // }
  // RX_AST_NODES(M)
  // #undef M

  AstAllocator() = default;
  AstAllocator(AstAllocator&&) = default;
private:
  AstAllocator(const AstAllocator&) = delete;
  AstNode* _freep = nullptr; // free nodes
};
