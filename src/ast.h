#pragma once
#include "srcloc.h"
#include "text.h"
#include "istr.h"
#include <string>

// Type of AST nodes.
// For each node, theres a AstType enum constant defined,
// e.g. for Ident its `AstIdent`.
#define RX_AST_NODES(_) \
  /* Name */ \
  _( Program ) \
  _( BoolConst ) \
  _( IntConst ) \
  _( DataTail ) \
  _( Literal ) \
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
  _( UnaryOp )  /*  op = value.i  */ \

#define RX_AST_NODES_DEFINED

enum AstType {
  AstNone = 0,
  #define M(Name) Ast##Name,
  RX_AST_NODES(M)
  #undef M
};

const std::string& ast_typename(AstType);

// Represents a node in a tree
struct AstNode {
  AstType  type;   // type of node
  SrcLoc   loc;    // location in source
  union {
    IStr     str;   // i.e. for an Ident, this is its name
    Text     text;  // i.e. for TextLit, this is its value
    uint64_t i;
    double   f;
  } value;

  // Sibling link (used for a child and for a free node)
  AstNode* nextSib = nullptr;

  // Children list
  AstNode* firstChild = nullptr;
  AstNode* lastChild = nullptr;

  // Add a node to end of child list
  void appendChild(AstNode& n) {
    if (firstChild == nullptr) {
      firstChild = &n;
    } else {
      lastChild->nextSib = &n;
    }
    lastChild = &n;
    lastChild->nextSib = nullptr;
  }

  // Add a node to beginning of child list
  void prependChild(AstNode& n) {
    n.nextSib = firstChild;
    firstChild = &n;
    if (lastChild == nullptr) {
      lastChild = &n;
    }
  }

  // Add a variable number of nodes, starting with `firstn`.
  // Nodes are expected to be linked by ->nextSib, and null-terminated.
  void appendChildren(AstNode& firstn) {
    AstNode* n = &firstn;

    if (firstChild == nullptr) {
      lastChild = firstChild = n;
      n = n->nextSib;
    }

    while (n != nullptr) {
      lastChild->nextSib = n;
      lastChild = n;
      n = n->nextSib;
    }
  }
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

private:
  AstNode* _freep = nullptr; // free nodes
  size_t   _nfree = 0;       // number of free nodes
};
