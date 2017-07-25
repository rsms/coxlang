#include "ast.h"
#include "freelist.h"

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
  for (auto cn = n.children.first; cn != nullptr; cn = cn->nextSib) {
    os << "\n";
    ast_repr(*cn, os, depth);
  }
  return os;
}


static std::ostream& repr_ty(AstNode& n, std::ostream& os, uint32_t depth) {
  if (n.ty != nullptr) {
    os << '<' << n.ty->repr(depth) << '>';
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

  repr_ty(n, os, depth);

  switch (n.type) {

    // no value, with children
    case AstBlock:
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
    case AstBool: {
      return os << (n.value.i ? "(Bool true)" : "(Bool false)");
    }

    // with int value =uint64, no children
    case AstIntConst: {
      return os << "(IntConst " << n.value.i << ')';
    }

    // with string value, no children
    case AstIdent: {
      return os << '(' << ast_typename(n.type) << ' ' << n.value.str << ')';
    }

    // with string text value, no children
    case AstString: {
      return os << '(' << ast_typename(n.type) << " \""
                << text::repr(n.value.str.data(), n.value.str.size()) << "\")";
    }
    case AstRawString: {
      return os << '(' << ast_typename(n.type) << " `"
                << text::repr(n.value.str.data(), n.value.str.size()) << "`)";
    }

    // with UChar value, with children
    case AstUnaryOp: {
      os << '(' << ast_typename(n.type) << ' '
         << text::encodeUTF8((UChar)n.value.i);
      return ast_reprchild(n, os, depth+1) << ')';
    }

    case AstDataTail: break;
    case AstNone: break; // never reached (tested for earlier)
  }
  return os;
}

// Free list

struct SibLink {
  AstNode* get(AstNode& a) const { return a.nextSib; }
  void set(AstNode& a, AstNode* b) const { a.nextSib = b; }
};

struct ChildLink {
  AstNode* get(AstNode& a) const { return a.children.first; }
  void set(AstNode& a, AstNode* b) const { a.children.first = b; }
};

static FreeList<AstNode, SibLink, ChildLink> freelist;

AstNode* AstAllocator::alloc() {
  return freelist.alloc(_freep);
}

void AstAllocator::free(AstNode* n) {
  n->ty = nullptr; // TODO free?
  freelist.free(_freep, n);
}
