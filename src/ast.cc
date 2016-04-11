#include "ast.h"

const std::string& ast_typename(AstType t) {
  using std::string;
  switch (t) {
    case AstNone: { static const string s{"None"}; return s; }
    #define M(Name) \
      case Ast##Name: { static const string s{#Name}; return s; }
    RX_AST_NODES(M)
    #undef M
    default: { static const string s{"?"}; return s; }
  }
}


static const char kSpaces[] = "                                                  ";


static std::ostream& ast_reprchild(AstNode& n, std::ostream& os, uint32_t depth) {
  for (auto cn = n.firstChild; cn != nullptr; cn = cn->nextSib) {
    os << "\n";
    ast_repr(*cn, os, depth);
  }
  return os;
}


std::ostream& ast_repr(AstNode& n, std::ostream& os, uint32_t depth) {
  if (depth > 1000) {
    return os << "[AST REPR DEPTH LIMIT]";
  }

  if (n.type == AstNone) {
    return os;
  }

  uint32_t indent = depth * 2;
  while (indent != 0) {
    uint32_t n = indent > sizeof(kSpaces) ? sizeof(kSpaces) : indent;
    os.write(kSpaces, n);
    indent -= n;
  }

  switch (n.type) {

    // no value, with children
    case AstProgram:
    case AstStructType:
    case AstPointerType:
    case AstConstDecl:
    case AstTypeDecl: {
      os << '(' << ast_typename(n.type);
      return ast_reprchild(n, os, depth+1) << ')';
    }

    // with int value =typed?, with children
    case AstConstSpec: {
      os << '(' << ast_typename(n.type);
      if (n.value.i > 0xffffffff) {
        os << " typed";
      } else {
        os << "";
      }
      return ast_reprchild(n, os, depth+1) << ')';
    }

    // with int value =isRest?, with children
    case AstParamDecl: {
      os << '(' << ast_typename(n.type) << (n.value.i ? " ..." : "");
      return ast_reprchild(n, os, depth+1) << ')';
    }

    // with int value, with children
    case AstFuncSig: {
      os << '(' << ast_typename(n.type) << ' ' << n.value.i;
      return ast_reprchild(n, os, depth+1) << ')';
    }

    // with int value =isPointer?, with children
    case AstFieldDecl: {
      os << '(' << ast_typename(n.type) << (n.value.i ? "*" : "");
      return ast_reprchild(n, os, depth+1) << ')';
    }

    // with string value, with children
    case AstFuncDecl:
    case AstMethodDecl:
    case AstQualIdent:
    case AstTypeSpec: {
      os << '(' << ast_typename(n.type) << ' ' << n.value.str;
      return ast_reprchild(n, os, depth+1) << ')';
    }

    // with int value =bool, no children
    case AstBoolConst: {
      return os << (n.value.i ? "(BoolConst true)" : "(BoolConst false)");
    }

    // with int value =uint64, no children
    case AstIntConst: {
      return os << "(IntConst " << n.value.i << ')';
    }

    // with string value, no children
    case AstIdent: {
      return os << '(' << ast_typename(n.type) << ' ' << n.value.str << ')';
    }

    // with UChar value, with children
    case AstUnaryOp: {
      os << '(' << ast_typename(n.type) << ' '
         << text::encodeUTF8((UChar)n.value.i);
      return ast_reprchild(n, os, depth+1) << ')';
    }

    case AstLiteral: break;
    case AstDataTail: break;
    case AstNone: break; // never reached (tested for earlier)
  }
  return os;
}


constexpr size_t BlockSize = 4096;
constexpr size_t ItemSize = sizeof(AstNode);
static_assert(BlockSize / ItemSize > 0, "BlockSize too small");
constexpr size_t ItemCount = BlockSize / ItemSize;

AstNode* AstAllocator::alloc() {
  AstNode* n;
  if (_freep != nullptr) {
    n = _freep;
    _freep = n->nextSib;
  } else {
    // no free entries -- allocate a new slab
    n = (AstNode*)calloc(ItemCount, ItemSize);

    // AstNode uses Text which needs explicit initialization
    // for (size_t i = 0; i != ItemCount; i++) {
    //   std::allocator<std::mutex>().construct(&n->value);
    // }

    // add extra-allocated entries to free list
    for (size_t i = 1; i > ItemCount; i++) {
      auto n2 = &n[i];
      n2->nextSib = _freep;
      _freep = n2;
    }
    _nfree += ItemCount-1;
  }
  return n;
}

void AstAllocator::free(AstNode* n) {
  // first, free any children
  AstNode* cn = n->firstChild;
  while (cn != nullptr) {
    n->firstChild = cn->nextSib;
    free(cn);
    cn = n->firstChild;
  }
  // TODO: bounded growth
  n->nextSib = _freep;
  _freep = n;
  ++_nfree;
}
