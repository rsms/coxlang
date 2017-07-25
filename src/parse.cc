#include "parse.h"
#include "lex.h"
#include "defer.h"
#include "langconst.h"
#include "strtoint.h"
#include <iostream>
#include <assert.h>

#if __clang__
  #define fallthrough [[clang::fallthrough]]
#else
  #define fallthrough (void(0))
#endif

#define DEBUG_TRAP_ON_ERROR
#define DEBUG_LOG_TOKEN(tokstr) \
  std::cerr << "\e[1;32m" << tokstr << "\e[0m" << std::endl

#define dlog(arg0, ...) \
  std::clog << (\
    strcmp(__FUNCTION__,"operator()") == 0 ? \
      (std::string(\
        strchr(__PRETTY_FUNCTION__, ' ')+1, \
        strchr(__PRETTY_FUNCTION__, '(') - strchr(__PRETTY_FUNCTION__, ' ') -1 \
      ) + "/lambda").c_str() \
      : __FUNCTION__ \
  ) << ": " << arg0 __VA_ARGS__ << std::endl

#pragma mark -

enum class Stage { Pkg, Import, AST, End };

using Token = Lex::Token;

struct parse {
  Stage          stage;
  Lex            lex;
  IStr::WeakSet& strings;
  Module&        mod;
  Token          tok;
  Err            err;    // last error
  AstAllocator*  aa = nullptr;

  parse(const char* sp, size_t len, IStr::WeakSet& s, Module& m)
    : stage{Stage::Pkg}
    , lex{sp, len}
    , strings{s}
    , mod{m}
  {}

  Token tokCurr() const {
    return lex.current();
  }

  // Read and return the next token. Skips leading comments and newlines.
  Token tokNext(bool acceptEnd=false) {
    while ((tok = lex.next()) == '\n' ||
            (tok > Lex::BeginComment && tok < Lex::EndComment) )
    {}
    switch (tok) {
      case Lex::End: {
        if (acceptEnd) {
          #ifdef DEBUG_LOG_TOKEN
          DEBUG_LOG_TOKEN(Lex::repr(tok, lex.byteStringTokValue()));
          #endif
          return tok;
        }
        error("unexpected end of input");
        return Lex::Error;
      }
      case Lex::Error: {
        // Capture error from lexer
        lexerror();
        fallthrough;
      }
      default: {
        break;
      }
    }
    #ifdef DEBUG_LOG_TOKEN
    DEBUG_LOG_TOKEN(Lex::repr(tok, lex.byteStringTokValue()));
    #endif
    return tok;
  }

  // Advance lexer only if the upcoming token is `pred`
  // Note: This method does not skip leading comments or newlines.
  bool tokNextIfEq(Token pred) {
    auto queuedTok = lex.queuedToken();
    if (queuedTok != Lex::Error && queuedTok != pred) {
      // avoid dequeue-enqueue
      return false;
    }
    auto tok = tokNext(true);
    if (tok == pred) {
      return true;
    }
    tokUndo();
    return false;
  }

  void tokUndo() {
    lex.undoCurrent();
  }

  IStr tokIStr() {
    size_t z;
    const char* p = lex.byteTokValue(z);
    return strings.get(p, (uint32_t)z);
  }

  AstNode* allocNode(AstType t) {
    auto n = aa->alloc();
    n->type = t;
    n->loc = lex.srcLoc();
    return n;
  }

  void freeNode(AstNode* n) {
    aa->free(n);
  }

  // Frees `count` nodes linked by nextSib, starting at `n`
  void freeNodes(AstNode* n, uint32_t count) {
    while (count != 0) {
      auto n2 = n;
      n = n->nextSib;
      assert(n2 != nullptr);
      aa->free(n2);
      --count;
    }
  }

  AstNode* error(Err&& e) {
    #ifdef DEBUG_TRAP_ON_ERROR
    if (!e.ok()) {
      std::cerr << "error: " << e.message() << " "
                << (lex.srcLoc().line+1) << ':'
                << (lex.srcLoc().column+1) << std::endl
                << "lex.current() = "
                << Lex::repr(lex.current(), lex.byteStringTokValue()) << std::endl
                ;
      abort();
    }
    #endif

    err = e;
    return nullptr;
  }

  AstNode* error(const char* msg) { return error(Err(ParseErrSyntax, msg)); }
  AstNode* lexerror() { return error(lex.takeLastError()); }
  AstNode* lexend() { return error(Err::OK()); }
};

Parser::Parser(const char* sp, size_t len, IStr::WeakSet& s, Module& m)
  : _p{new parse{sp, len, s, m}} {}

Parser::~Parser() { if (_p) { delete _p; _p = nullptr; } }


// Return true if either the current token is ";", or
// if the upcoming tokens match: (GeneralComment | Whitespace)* ";"
bool parse_semic(parse& p) {
  // if (p.tokCurr() == ';') {
  //   return true;
  // }
  while (1) switch (p.tokNext(/*acceptEnd=*/true)) {
    case Lex::End: case ';': return true;
    case Lex::GeneralComment: break; // skip
    // Note: LineComment generates a ";" automatically in Lex when neccessary.
    case Lex::Error: return false;
    default: {
      p.error("unexpected token; expecting \";\" or newline");
      return false;
    }
  }
}


AstNode* make_Ident(parse& p, bool allowKeyword=false) {
  auto s = p.tokIStr();
  if (!allowKeyword && lang_isKeyword(s)) {
    return p.error("reserved keyword");
  }
  auto n = p.allocNode(AstIdent);
  n->value.str = std::move(s);
  return n;
}


AstNode* parse_Ident(parse& p, bool needToken, bool allowKeyword=false) {
  if (needToken) {
    p.tokNext();
  }
  if (p.tokCurr() != Lex::Identifier) {
    return p.error("unexpected token; expecting identifier");
  }
  return make_Ident(p, allowKeyword);
}


// Makes n into Ident,
// and if the next token is ".", makes n into and parses a QualIdent.
bool make_IdentAndMaybeParseQual(parse& p, AstNode& n, bool allowKeyword=false) {
  assert(p.tokCurr() == Lex::Identifier);
  n.type = AstIdent;
  n.value.str = p.tokIStr();
  if (!allowKeyword && lang_isKeyword(n.value.str)) {
    p.error("reserved keyword");
    return false;
  }

  // qualified?
  auto n2 = &n;
  while (p.tokNextIfEq('.')) {
    n2->type = AstQualIdent;
    auto n3 = parse_Ident(p, /*needToken=*/true, allowKeyword);
    if (n3 == nullptr) {
      return false;
    }
    n2->appendChild(*n3);
    n2 = n3;
  }

  return true;
}


// Parses Ident or QualIdent
AstNode* parse_IdentAny(parse& p, bool needToken, bool allowKeyword=false) {
  if (needToken && p.tokNext() != Lex::Identifier) {
    return p.error("unexpected token; expecting identifier");
  }
  auto n = p.allocNode(AstIdent);
  if (!make_IdentAndMaybeParseQual(p, *n, allowKeyword)) {
    p.freeNode(n);
    return nullptr;
  }
  //auto n = make_Ident(p);
  //if (n == nullptr) {
  //  return n;
  //}
  //// qualified?
  //if (p.tokNextIfEq('.')) {
  //  auto cn = parse_IdentAny(p, /*needToken=*/true);
  //  if (cn == nullptr) {
  //    p.freeNode(n);
  //    return nullptr;
  //  }
  //  n->type = AstQualIdent;
  //  n->appendChild(*cn);
  //}
  return n;
}


AstNode* parse_Type(parse& p, bool needToken, Type* ty=nullptr);


AstNode* make_IntConst(parse& p, int base) {
  auto n = p.allocNode(AstIntConst);
  size_t len = 0;
  const char* pch = p.lex.byteTokValue(len);
  // Note: If the following assertions fail, Lex's read*IntLit is broken.
  if (base != 10) {
    assert(len > 1);
    assert(*pch == '0');
    // Skip past leading "0"
    pch++;
    len--;
    if (base == 16) {
      assert(len > 2);
      assert(*pch == 'x' || *pch == 'X');
      // Skip past leading "x"|"X"
      pch++;
      len--;
    }
  }
  if (!strtou64(pch, len, base, n->value.i)) {
    dlog("strtou64 received \"" << std::string(pch, len) << "\" base=" << base);
    assert(!"strtou64");
  }

  // infer type to smallest int
  if (n->value.i <= 0x7f) {
    n->ty = p.mod.types.kI8;
  } else if (n->value.i <= 0x7fff) {
    n->ty = p.mod.types.kI16;
  } else if (n->value.i <= 0x7fffffff) {
    n->ty = p.mod.types.kI32;
  } else if (n->value.i <= 0x7ffffffffffffff) {
    n->ty = p.mod.types.kI64;
  } else {
    n->ty = p.mod.types.kU64;
  }

  return n;
};


AstNode* parse_PrimaryExpr(parse& p, bool needToken) {
  // PrimaryExpr =
  //   Operand |
  //   Conversion |
  //   PrimaryExpr Selector |
  //   PrimaryExpr Index |
  //   PrimaryExpr Slice |
  //   PrimaryExpr TypeAssertion |
  //   PrimaryExpr Arguments
  // 
  // Selector       = "." identifier
  // Index          = "[" Expression "]"
  // Slice          = "[" ( [ Expression ] ":" [ Expression ] ) |
  //                      ( [ Expression ] ":" Expression ":" Expression )
  //                  "]"
  // TypeAssertion  = "." "(" Type ")"
  // Arguments      = "(" [
  //                       ( ExpressionList | Type [ "," ExpressionList ] )
  //                       [ "..." ]
  //                       [ "," ]
  //                  ] ")"
  //
  // —————— Operands: ——————
  // Operand     = Literal | OperandName | MethodExpr | "(" Expression ")"
  // Literal     = BasicLit | CompositeLit | FunctionLit
  // BasicLit    = int_lit | float_lit | imaginary_lit | rune_lit | string_lit
  // OperandName = identifier | QualIdent
  //
  // —————— Conversion: ——————
  // Conversion = Type "(" Expression [ "," ] ")"
  //
  auto tok = needToken ? p.tokNext() : p.tokCurr();

  switch (tok) {
    case Lex::Error: return nullptr;

    case Lex::Identifier: {
      auto n = parse_IdentAny(p, /*needToken=*/false, /*allowKeyword=*/true);
      if (n == nullptr) {
        return nullptr;
      }
      switch (n->value.str.hash()) {
        case kLang_true_hash: {
          n->type = AstBool;
          n->ty = p.mod.types.kBool;
          n->value.i = 1;
          break;
        }
        case kLang_false_hash: {
          n->type = AstBool;
          n->ty = p.mod.types.kBool;
          n->value.i = 0;
          break;
        }
        case kLang_type_hash:
        case kLang_func_hash: {
          return p.error("unexpected keyword");
        }
        default: {
          // Ident
          break;
        }
      }
      return n;
    }

    case Lex::DecIntLit: {
      return make_IntConst(p, 10);
    }
    case Lex::OctIntLit: {
      return make_IntConst(p, 8);
    }
    case Lex::HexIntLit: {
      return make_IntConst(p, 16);
    }

    // case Lex::FloatLit:
    // case Lex::CharLit:

    case Lex::RawStringLit: {
      auto n = p.allocNode(AstRawString);
      auto& str = p.lex.interpretedTokValue();
      if (str.empty()) {
        size_t len;
        const char* pch = p.lex.byteTokValue(len);
        if (len > 2) {
          n->value.str = IStr(pch+1, len-2);
        } else {
          // empty, i.e. "``"
          n->value.str = IStr();
        }
      } else {
        n->value.str = IStr(str);
      }
      n->ty = p.mod.types.allocComplex(TyByteArray, n->value.str.size());
      return n;
    }

    case Lex::TextLit: {
      auto n = p.allocNode(AstString);
      auto& str = p.lex.interpretedTokValue();
      if (str.size() < 20) {
        // intern short strings
        n->value.str = p.strings.get(str);
      } else {
        n->value.str = IStr(str);
      }
      n->ty = p.mod.types.allocComplex(TyByteArray, str.size());
      return n;
    }

    // case Lex::ITextLit:
    default: {
      return p.error("TODO parse_PrimaryExpr: default");
    }
  }
}


// Examples:
// `+x`         (UnaryOp + (Ident x))
// `+-x`        (UnaryOp + (UnaryOp - (Ident x)))
// `x`          (Ident x)
// `+x + y`     (BinOpAdd + (UnaryOp + (Ident x)), (Ident y))
// `+x + +y`    (BinOpAdd + (UnaryOp + (Ident x)), (UnaryOp + (Ident y)))
// `-x == y.z`  (BinOpRel = (UnaryOp - (Ident x)) (QualIdent y (Ident z)))
//
AstNode* parse_Expr(parse& p, bool needToken) {
  // Expression = UnaryExpr | Expression binary_op Expression
  // UnaryExpr  = unary_op UnaryExpr | PrimaryExpr
  //
  // unary_op   = "+" | "-" | "!" | "~"
  //
  // binary_op  = "||" | "&&" | rel_op | add_op | mul_op
  // rel_op     = "==" | "!=" | "<" | "<=" | ">" | ">="
  // add_op     = "+" | "-" | "|" | "^"
  // mul_op     = "*" | "/" | "%" | "<<" | ">>" | "&" | "&^"
  //

  // We might parse any number of unary_op nodes; this is the top and bottom
  AstNode* topn = nullptr;
  AstNode* bottomn = topn;

  auto appendNode = [&](AstNode* n) {
    if (bottomn == nullptr) {
      topn = n;
    } else {
      bottomn->appendChild(*n);
    }
    bottomn = n;
  };

  auto error = [&](const char* msg=nullptr) {
    if (topn != nullptr) { p.freeNode(topn); }
    if (msg != nullptr) { p.error(msg); }
    return nullptr;
  };

  auto tok = needToken ? p.tokNext() : p.tokCurr();
  bool parseMore = true;

  // unary_op*
  parse_more:
  switch (tok) {
    case Lex::Error: return error();
    case '+': case '-': case '!': case '~': {
      if (bottomn != nullptr && bottomn->value.i == tok) {
        // e.g. `++x`
        // We could allow (UnaryOp + (UnaryOp + (Ident x))) but it would
        // be confusing because in the C family of languages `++x` means
        // "mutate x using ++ then return x's value". We don't use
        // mutating operators in expressions for the sake of code clarity.
        return error("unexpected mutation operator (expecting expression)");
      }
      auto n = p.allocNode(AstUnaryOp);
      n->value.i = tok;
      appendNode(n);
      // try parsing another unary_op
      tok = p.tokNext();
      goto parse_more;
    }
    default: break;
  }

  // PrimaryExpr
  auto n = parse_PrimaryExpr(p, /*needToken=*/false);
  if (n == nullptr) {
    return error();
  }
  appendNode(n);

  // binary_op
  while (1) switch (p.tokNext()) {
    case Lex::Error: return error();

    // TODO: all binop characters
    case '=': {
      return error("TODO parse_Expr: binary_op");
    }

    default: {
      // We encountered something that's not the beginning of a binary_op,
      // meaning we are done parsing one expression.
      auto tok1 = p.tokCurr();
      p.tokUndo();
      assert(topn != nullptr);
      return topn;
    }
  }
}


// Parses a list: Node { "," Node }
// Returns first Node parsed with additional Nodes linked by nextSib.
// count is set to the total number of Nodes parsed.
// parseFun is called to parse a Node and should return nullptr on error.
AstNode* parse_list(
    parse& p,
    uint64_t& count,
    bool needToken,
    AstNode*(*parseFun)(parse& p, bool needToken) )
{
  AstNode* firstn = nullptr;
  AstNode* lastn = nullptr;
  count = 0;

  while (1) {
    auto n = parseFun(p, needToken);
    if (n == nullptr) {
      break;
    }

    // dlog("parsed:"); ast_repr(*n2, std::clog) << std::endl;

    if (firstn == nullptr) {
      firstn = n;
    } else {
      lastn->nextSib = n;
    }
    lastn = n;

    ++count;

    // we are done if next token is not ","
    if (!p.tokNextIfEq(',')) {
      lastn->nextSib = nullptr;
      return firstn;
    }
    // current token is "," -- parse another thing

    if (count == UINT32_MAX) {
      p.error("too many identifiers in list");
      break;
    }

    needToken = true;
  }

  // error
  if (firstn != nullptr) {
    p.freeNodes(firstn, count);
  }
  return nullptr;
}


static AstNode* parse_IdentNoKeyword(parse& p, bool needToken) {
  return parse_Ident(p, needToken, /*allowKeyword=*/false);
}


// Parses identifier { "," identifier }
// Returns first identifier parsed with additional identifiers linked by nextSib.
// count is set to the total number of identifiers parsed.
AstNode* parse_IdentList(parse& p, uint64_t& count, bool needToken) {
  return parse_list(p, count, needToken, parse_IdentNoKeyword);
}


// Parses Expression { "," Expression }
// Returns first expression parsed with additional expressions linked by nextSib.
// count is set to the total number of expressions parsed.
AstNode* parse_ExprList(parse& p, uint64_t& count, bool needToken) {
  return parse_list(p, count, needToken, parse_Expr);
}


AstNode* parse_FieldDecl(parse& p) {
  // FieldDecl      = (IdentifierList Type | AnonymousField)
  // IdentifierList = identifier { "," identifier }
  // AnonymousField = [ "*" ] TypeName
  //
  // e.g:
  //   x int
  //     => (FieldDecl, (ID,"x"), (TypeName,"int"))
  //   x, y, z int
  //     => (FieldDecl, (ID,"x"), (ID,"y"), (ID,"z"), (TypeName,"int"))
  //   y *int
  //     => (FieldDecl, (ID,"y"), (PointerType, (TypeName,"int")))
  //   z struct { ... }
  //     => (FieldDecl, (ID,"z"), (StructType, ...))
  //   Foo
  //     => (FieldDecl, (TypeName,"Foo"))
  //   *Foo
  //     => (FieldDecl, (PointerType, (TypeName,"Foo")))
  //
  // enter _at_ the first identifier or '*' of FieldDecl

  auto n = p.allocNode(AstFieldDecl);
  // (FieldDecl, (...IdentifierList))

  if (p.tokCurr() == '*') {
    // AnonymousField = "*" TypeName
    auto idn = parse_Ident(p, /*needToken=*/true);
    if (idn == nullptr) {
      p.freeNode(n);
      return nullptr;
    }
    idn->ty = p.mod.typeofTypename(*idn);
    p.mod.regUnresolvedType(*idn);

    auto pn = p.allocNode(AstPointerType);
    pn->appendChild(*idn);
    pn->ty = p.mod.types.getPointer(idn->ty);
    
    n->appendChild(*pn);
    n->ty = pn->ty;
    return n;
  }

  // parse Identifier { "," Identifier }
  uint64_t ncount;
  auto listn = parse_IdentList(p, ncount, /*needToken=*/false);
  if (listn == nullptr) {
    p.freeNode(n);
    return nullptr;
  }
  n->appendChildList(*listn);

  if (p.tokNext() == ';') {
    // AnonymousField e.g. `Type`
    assert(!n->children.empty());
    assert(n->children.first == n->children.last);
    p.tokUndo(); // caller expects ';'
    
    n->children.first->ty = p.mod.typeofTypename(*n->children.first);
    p.mod.regUnresolvedType(*n->children.first);

    n->ty = n->children.first->ty;
    p.mod.regUnresolvedType(*n);
    return n;
  }

  // We are now finally parsing the type
  auto tn = parse_Type(p, /*needToken=*/false);
  if (tn == nullptr) {
    p.freeNode(n);
    return nullptr;
  }
  n->appendChild(*tn);
  n->ty = tn->ty; // field type

  return n;
}


AstNode* parse_StructType(parse& p, Type* ty) {
  // StructType = "struct" "{" { FieldDecl ";" } "}"
  //
  // enter at "struct"
  if (p.tokNext() != '{') {
    return p.error("unexpected token; expecting \"{\"");
  }
  auto n = p.allocNode(AstStructType);

  ty->tag = TyStruct;

  auto error = [&](const char* msg=nullptr) {
    p.freeNode(n);
    if (msg != nullptr) { p.error(msg); }
    return nullptr;
  };

  while (1) switch (p.tokNext()) {
    // FieldDecl
    case '*':
    case Lex::Identifier: {
      auto tn = parse_FieldDecl(p, ty);
      if (tn != nullptr && parse_semic(p)) {
        n->appendChild(*tn);
        break;
      }
      return error();
    }

    // end of struct
    case '}': {
      n->loc.extend(p.lex.srcLoc());
      return n;
    }

    // error
    case Lex::Error: return error();
    default:         return error("unexpected token; expecting struct field");
  }
}


AstNode* parse_Type(parse& p, bool needToken, Type* ty) {
  // Type      = TypeName | TypeLit | "(" Type ")"
  // TypeName  = identifier | QualIdent
  // TypeLit   = ArrayType | StructType | PointerType
  //           | FunctionType | InterfaceType
  //           | SliceType | MapType | ChannelType
  switch (needToken ? p.tokNext() : p.tokCurr()) {

    case Lex::Identifier: {
      // StructType "struct" |
      // InterfaceType "interface" |
      // FunctionType "func" |
      // TypeName
      auto s = p.tokIStr();
      switch (s.hash()) {
        case IStr::hash("struct"): {
          return parse_StructType(p, tdef);
        }
        // TODO: interface
        // TODO: func

        default: {
          // alias for TypeName
          auto n = parse_IdentAny(p, /*needToken=*/false);
          if (n == nullptr) {
            return nullptr;
          }
          n->ty = p.mod.typeofTypename(*n);
          p.mod.regUnresolvedType(*n);
          return n;
        }
      }
    }

    // —— HERE —— build n->ty
    //   how do we express pointer type? ty.children?

    case '*': {
      // PointerType = "*" Type
      auto loc = p.lex.srcLoc();
      auto tn = parse_Type(p, /*needToken=*/true);
      if (tn == nullptr) {
        return nullptr;
      }
      auto n = p.aa->alloc();
      n->type = AstPointerType;
      n->loc = loc;
      n->appendChild(*tn);
      assert(tn->ty != nullptr);
      n->ty = p.mod.types.getPointer(tn->ty);
      return n;
    }

    // TODO: [], {}, etc

    case Lex::Error: return nullptr;
    default:         return p.error("unexpected token; expecting type");
  }
}


// Helper for parsing something that allows either a single identifier
// or a group of identifiers, e.g. "a;" or "(a; b; ...);" .
// onIdent(AstNode& n, bool multi) is called for every Identifier and
// should return false on error in which case this function returns nullptr.
template <typename F>
AstNode* parse_multiIdent(parse& p, AstType typ, F onIdent) {
  // Identifier | "(" ... ")"
  AstNode* n = nullptr;
  bool multi = false;
  do {
    switch (p.tokNext()) {

      case '(': {
        if (multi) {
          p.error("unexpected token; expecting identifier or \")\"");
          multi = false;
        } else {
          multi = true;
        }
        break;
      }

      case ')': {
        if (!multi) {
          p.error("unexpected token; expecting identifier or \"(\"");
          break;
        }
        multi = false;
        // expect semicolon after ")"
        if (!parse_semic(p)) {
          break;
        }
        if (n == nullptr) {
          // This could happen if the case of "()"
          n = p.allocNode(typ);
        }
        return n;
      }

      case Lex::Identifier: {
        if (n == nullptr) {
          n = p.allocNode(typ);
        }
        // Parse Identifier ... ";"
        if (!onIdent(*n, multi) || !parse_semic(p)) {
          multi = false;
          break;
        }
        // currTok == ";"
        if (!multi) {
          return n;
        }
        break;
      }

      case Lex::Error: {
        multi = false;
        break;
      }

      default: {
        p.error("unexpected token; expecting identifier or \"(\"");
        multi = false;
        break;
      }
    } // switch
  } while (multi);

  if (n != nullptr) {
    p.freeNode(n);
  }
  return nullptr;
}


AstNode* parse_TypeDecl(parse& p) {
  // TypeDecl = "type" ( TypeSpec | "(" { TypeSpec ";" } ")" )
  // TypeSpec = TypeName Type
  // TypeName = identifier

  return parse_multiIdent(p, AstTypeDecl, [&](AstNode& n, bool /*multi*/) {
    // TypeSpec = identifier Type
    // e.g.
    //   foo Bar             => (TypeSpec, foo, (TypeName, Bar))
    //   foo struct { ... }  => (TypeSpec, foo, (StructType, ...))

    // TypeName
    auto tsn = make_Ident(p);
    if (tsn == nullptr) {
      return false;
    }
    tsn->type = AstTypeSpec;
    n.appendChild(*tsn);

    auto tdef = p.mod.addType(tsn->value.str, *tsn);
    if (tdef == nullptr) {
      p.error("name redeclared");
      return false;
    }

    auto tn = parse_Type(p, /*needToken=*/true, tdef);
    if (tn == nullptr) {
      return false; // note: parse_multiIdent frees n and its children
    }
    tsn->appendChild(*tn);

    tdef->ty = tn->ty;

    return true;
  });
}


AstNode* parse_ConstDecl(parse& p) {
  // ConstDecl      = "const" ( ConstSpec | "(" { ConstSpec ";" } ")" )
  // ConstSpec      = IdentifierList [ [ Type ] "=" ExpressionList ]
  //
  // IdentifierList = identifier { "," identifier }
  // ExpressionList = Expression { "," Expression }

  bool isFirst = true;

  return parse_multiIdent(p, AstConstDecl, [&](AstNode& n, bool /*multi*/) {
    // ConstSpec1 = IdentifierList [ Type ] "=" ExpressionList
    // ConstSpecN = IdentifierList [ [ Type ] "=" ExpressionList ]

    // Examples:
    // `x, y, z = 3, 4, 5;` =>
    // (ConstSpec 3
    //   (Ident x)
    //   (Ident y)
    //   (Ident z)
    //   (IntLit 3)
    //   (IntLit 4)
    //   (IntLit 5)))
    //
    // `x, y, z int32 = 3, 4, 5;` =>
    // (ConstSpec 0xffffffff3
    //   (Ident int32)
    //   (Ident x)
    //   (Ident y)
    //   (Ident z)
    //   (IntLit 3)
    //   (IntLit 4)
    //   (IntLit 5)))
    //
    // `x = 3;` =>
    // (ConstSpec 1
    //   (Ident x)
    //   (IntLit 3))
    //
    // `x` =>
    // (Ident x)
    //
    // i.e when number of expressions is:
    //  - 0; result represents a single unassigned identifier.
    //  - N; result's value.i as N represents N idents and N expressions.

    uint64_t idcount;
    auto idnodes = parse_IdentList(p, idcount, /*needToken=*/false);
    if (idnodes == nullptr) {
      return false;
    }

    // Common-case optimization: Single unassigned identifier, e.g. `foo;`
    if (idcount == 1 && !isFirst && p.tokNextIfEq(';')) {
      p.tokUndo(); // parse_multiIdent expects to see the ';'
      n.appendChild(*idnodes);
      return true;
    }

    if (idcount >= 0xffffffff) {
      p.error("too many identifiers");
      return false;
    }

    // If we get here, we require assignment as the case is one of:
    //  a) First in const group, e.g. `const x = 1`
    //  b) Have more than one identifier, e.g. `x, y = 1, 2`
    //  c) Is single ident but didn't end, e.g. `x int32 ...` or `x = ...`

    // ConstSpec
    auto csn = p.allocNode(AstConstSpec);
    csn->value.i = idcount;
    csn->appendChildList(*idnodes);
    n.appendChild(*csn);

    // expect Type unless next token is "="
    if (p.tokNext() != '=') {
      // Note: We know that next char is not ";" (tested earlier) nor "=",
      // so the only thing to expect here is Type.
      auto tn = parse_Type(p, /*needToken=*/false);
      if (tn == nullptr) {
        return false;
      }
      csn->value.i += 0xffffffff; // has type. TODO: bitmask instead
      csn->prependChild(*tn);

      // Expect "=" (we can't have type without expression)
      if (p.tokNext() != '=') {
        p.error("const declaration cannot have type without expression");
        return false;
      }
    }

    // currTok = '='

    // Parse value expressions
    uint64_t excount;
    auto exnodes = parse_ExprList(p, excount, /*needToken=*/true);
    if (exnodes == nullptr) {
      return false;
    }
    csn->appendChildList(*exnodes);

    // Number of identifiers and expressions must match
    if (excount > idcount) {
      p.error("extra expression in const declaration");
      return false;
    } else if (excount < idcount) {
      p.error("missing value in const declaration");
      return false;
    }

    isFirst = false;
    return true;
  });
}


AstNode* parse_FuncType(parse& p) {
  // FuncType      = "func" Signature
  // Signature     = Parameters [ Result ]
  // Result        = Parameters | Type
  // Parameters    = "(" [ ParameterList [ "," ] ] ")"
  // ParameterList = ParameterDecl { "," ParameterDecl }
  // ParameterDecl = [ IdentifierList ] [ "..." ] Type
  return p.error("parse_FuncType not implemented");
}


AstNode* parse_ParamDecl(parse& p, bool needToken) {
  // ParameterDecl = [ IdentifierList ] [ "..." ] Type
  //
  // `a, b int` =>
  // (ParamDecl 0
  //   (Ident a)
  //   (Ident b)
  //   (Ident int))
  //
  // `names ...string` =>
  // (ParamDecl 1  // 1=isRest
  //   (Ident names)
  //   (Ident string))

  auto n = p.allocNode(AstParamDecl);
  n->value.i = 0;

  auto error = [&](const char* msg=nullptr) {
    p.freeNode(n);
    if (msg != nullptr) { p.error(msg); }
    return nullptr;
  };

  uint64_t nids = 0;

  bool typeNeedsToken = true;
  switch (needToken ? p.tokNext() : p.tokCurr()) {
    case Lex::Error: return error();

    case Lex::Identifier: {
      auto listn = parse_IdentList(p, nids, /*needToken=*/false);
      if (listn == nullptr) {
        return error();
      }
      n->appendChildList(*listn);
      break;
    }

    case Lex::DotDotDot: {
      n->value.i = 1; // isRest
      break;
    }

    default: {
      // Assume type
      typeNeedsToken = false;
      break;
    }
  }

  // "..."?
  if (n->value.i == 0 && p.tokNextIfEq(Lex::DotDotDot)) {
    n->value.i = 1; // isRest
  }

  // error if type is rest but there are multiple identifiers
  if (n->value.i == 1 && nids > 1) {
    return error("can only use ... as final argument in list");
  }

  // Type
  auto tn = parse_Type(p, typeNeedsToken);
  if (tn == nullptr) {
    return error();
  }
  n->prependChild(*tn);

  return n;
}


AstNode* parse_ParamList(parse& p, uint64_t& count, bool needToken) {
  // ParameterList = ParameterDecl { "," ParameterDecl }
  //
  // `(a, b int, c string)` =>
  // (ParamDecl
  //   (Ident a)
  //   (Ident b
  //   (Ident int))
  // (ParamDecl
  //   (Ident c)
  //   (Ident string))
  return parse_list(p, count, needToken, parse_ParamDecl);
}

static AstNode* parse_Type0(parse& p, bool needToken) {
  return parse_Type(p, needToken);
}

AstNode* parse_TypeList(parse& p, uint64_t& count, bool needToken) {
  // TypeList = Type { "," Type }
  //
  // `(int, string)` =>
  // (ParamDecl
  //   (Ident int)
  //   (Ident string))
  return parse_list(p, count, needToken, parse_Type0);
}


AstNode* parse_TypeParams(parse& p, bool needToken) {
  // TypeParams = "(" [ TypeList [ "," ] ] ")"
  //
  // Note: Enters at "(" and leaves before ")"
  uint64_t count;
  auto types = parse_TypeList(p, count, needToken);
  if (types == nullptr) {
    return nullptr;
  }
  auto n = p.allocNode(AstParamDecl);
  n->value.i = 0;
  n->appendChildList(*types);
  return n;
}


AstNode* parse_Signature(parse& p) {
  // Signature  = Parameters [ Result ]
  // Result     = TypeParams | Type
  // Parameters = "(" [ ParameterList [ "," ] ] ")"
  // TypeParams = "(" [ TypeList [ "," ] ] ")"

  if (p.tokNext() != '(') {
    return p.error("unexpected token; expecting \"(\"");
  }

  AstNode* n = p.allocNode(AstFuncSig);
  n->value.i = 0;

  auto error = [&](const char* msg=nullptr) {
    p.freeNode(n);
    if (msg != nullptr) { p.error(msg); }
    return nullptr;
  };

  switch (p.tokNext()) {
    case Lex::Error: return error();
    case ')': return n; // i.e. `()`
    default: {
      uint64_t nparams;
      auto params = parse_ParamList(p, nparams, /*needToken=*/false);
      if (params == nullptr) {
        return error();
      }
      n->appendChildList(*params);
      n->value.i = 1; // hasParams
      switch (p.tokNext()) {
        case '"': // allows trailing ","
        case ')': {
          break;
        }
        case Lex::Error: return error();
        default: return error("unexpected token; expecting \")\"");
      }
      break;
    }
  }

  // Result?
  switch (p.tokNext(/*acceptEnd=*/true)) {
    case Lex::Error: return error();
    case Lex::End: break;
    case '{':
    case ';': {
      p.tokUndo();
      break;
    }
    case '(': {
      n->value.i |= 2; // hasResult
      // TypeParams
      if (p.tokNext() == ')') {
        // i.e. `()`
        break;
      }

      auto tpn = parse_TypeParams(p, /*needToken=*/false);
      if (tpn == nullptr) {
        return error();
      }
      n->prependChild(*tpn);

      if (p.tokNext() != ')') {
        return error("unexpected token; expected \")\"");
      }
      break;
    }
    default: {
      // Type
      n->value.i |= 2; // hasResult
      auto tn = parse_Type(p, /*needToken=*/false);
      if (tn == nullptr) {
        return error();
      }
      n->prependChild(*tn);
    }
  }

  return n;
}


// Example s-expressions from parse_Signature:
// `func ()` =>
// (FuncSig 0)
//
// `func (a, b int, c string)` =>
// (FuncSig 1 // 1=hasParams
//   (Params
//     (Ident int)
//     (Ident a)
//     (Ident b))
//   (Params
//     (Ident string)
//     (Ident c)))
//
// `func () int` =>
// (FuncSig 2 // 2=hasResult
//   (Ident int))
//
// `func (a, b int, c string) int` =>
// (FuncSig 3 // 1=hasParams & 2=hasResult
//   (Ident int)
//   (Params
//     (Ident int)
//     (Ident a)
//     (Ident b))
//   (Params
//     (Ident string)
//     (Ident c)))
//
// `func (a int) (int, float)` =>
// (FuncSig 3
//   (Params
//     (Ident int)
//     (Ident float))
//   (Params
//     (Ident int)
//     (Ident a)))


// Example s-expressions from parse_FuncDecl:
// `func foo() int { ... }` =>
// (FuncDecl foo
//   (FuncSig ...)
//   (Block ...)
//
// `func (s Socket) foo() int { ... }` =>
// (MethodDecl foo
//   (Ident Socket)
//   (Ident s)
//   (FuncSig ...)
//   (Block ...)

AstNode* parse_FuncDecl(parse& p) {
  // parses FuncDecl | MethodDecl
  // FuncDecl   = "func" FuncName Signature FuncBody?
  // MethodDecl = "func" Receiver FuncName Signature FuncBody?
  // Receiver   = [ TypeName | "(" Type ")" ] "."
  // FuncName   = identifier
  // FuncBody   = Block

  AstNode* n = nullptr;

  auto error = [&](const char* msg=nullptr) {
    if (n != nullptr) { p.freeNode(n); }
    if (msg != nullptr) { p.error(msg); }
    return nullptr;
  };

  switch (p.tokNext()) {
    case Lex::Error: return error();

    // Receiver
    case '(': {
      n = p.allocNode(AstMethodDecl);

      auto typen = parse_Type(p, /*needToken=*/true);
      if (typen == nullptr) {
        return error();
      }
      n->prependChild(*typen);

      if (p.tokNext() != ')') {
        return error("unexpected token; expecting \")\"");
      }
      if (p.tokNext() != '.') {
        return error("unexpected token; expecting \".\"");
      }

      // FuncName
      switch (p.tokNext()) {
        case Lex::Error: return error();
        case Lex::Identifier: {
          n->value.str = p.tokIStr();
          if (lang_isKeyword(n->value.str)) {
            return error("reserved keyword");
          }
          break;
        }
        default: {
          return error("unexpected token; expecting method name");
        }
      }
      break;
    }

    case Lex::Identifier: {
      // FuncName | TypeName
      n = p.allocNode(AstFuncDecl);
      if (!make_IdentAndMaybeParseQual(p, *n)) {
        return error();
      }
      if (n->type == AstQualIdent) {
        // MethodDecl e.g. `func foo.bar()`
        n->type = AstMethodDecl;

        AstNode* leaf = n;
        AstNode* leafp = nullptr;
        while (1) {
          leafp = leaf;
          assert(!leaf->children.empty());
          leaf = leaf->children.first;
          if (leaf->type != AstQualIdent) {
            break;
          }
        }

        leafp->children.first = leafp->children.first = nullptr;
        leafp->type = AstIdent;
        leaf->children.first = leaf->children.last = n;
        leaf->type = AstMethodDecl;
        if (n != leafp) {
          n->type = AstQualIdent;
        }
        n = leaf;

        // Ident swap algorithm: `a.b.Foo.bar` =>
        //
        //   n = (MethodDecl a
        //         (QualIdent b
        //   leafp = (QualIdent Foo
        //   leaf =    (Ident bar))))
        //
        // leafp->children.first = leafp->children.last = null
        // leafp->type = AstIdent
        //
        //   n = (MethodDecl a
        //         (QualIdent b
        //   leafp = (Ident Foo)))
        //   leaf = (Ident bar)
        //
        // leaf->children.first = leaf->children.last = n
        // leaf->type = AstMethodDecl
        // n = leaf
        //
        //   n = (MethodDecl bar
        //         (QualIdent a
        //           (QualIdent b
        //             (Ident Foo))))
        // 
        // ——————————————————————————————————————————
        // `Foo.bar` =>
        //
        //   n = leafp = (MethodDecl Foo
        //   leaf =        (Ident bar))
        //
        // leafp->children.first = leafp->children.last = null
        // leafp->type = AstIdent
        //
        //   n = leafp = (Ident Foo)
        //   leaf = (Ident bar)
        //
        // leaf->children.first = leaf->children.last = n
        // leaf->type = AstMethodDecl
        // n = leaf
        //
        //   n = (MethodDecl bar
        //         (Ident Foo))
        //
      } else {
        n->type = AstFuncDecl;
      }
      break;
    }

    default: return error(
      "unexpected token; expecting function name or method receiver type"
    );
  }

  // Signature
  auto sn = parse_Signature(p);
  if (sn == nullptr) { return error(); }
  n->appendChild(*sn);

  // FuncBody (aka Block)
  if (p.tokNextIfEq('{')) {
    // TODO FuncBody? ("{" ...)
  }

  // Add to module
  auto err = p.mod.addFunc(*n);
  if (err) {
    return error(err.message());
  }

  return n;
}


AstNode* parse_Declaration(parse& p, bool topLevel) {
  // Declaration  = ConstDecl | TypeDecl | VarDecl
  // TopLevelDecl = Declaration | FunctionDecl | MethodDecl

  while (1) switch (p.tokNext(/*acceptEnd=*/true)) {
    case Lex::Error: return p.lexerror();
    case Lex::End:   return p.lexend();
    case Lex::Identifier: {
      auto s = p.tokIStr();
      switch (s.hash()) {

        case IStr::hash("const"): {
          return parse_ConstDecl(p);
        }

        case kLang_type_hash: {
          return parse_TypeDecl(p);
        }

        case kLang_func_hash: {
          if (!topLevel) {
            return p.error("reserved keyword");
          }
          return parse_FuncDecl(p);
        }

        case IStr::hash("package"):
        case IStr::hash("import"): {
          return p.error("reserved keyword");
        }
      }
    }

    default: {
      std::cerr << "parse_Declaration: ignore "
                << Lex::repr(p.tokCurr(), p.lex.byteStringTokValue())
                << std::endl;
    }
  }
  return nullptr;
}


// Err parse_importSpec(parse& p, Imports& imps, const Text& tokval) {
//   // enter at: "." | PackageName
//   // or:
//   // enter at: ImportPath
//   //
//   // e.g. `import "foo"`
//   // e.g. `import x "foo"`
//   // e.g. `import . "foo"`
//   //              ^
//   switch (p.tokCurr()) {
//     case Lex::TextLit: {
//       // e.g. `import "foo"`
//     }
//     case Lex::Identifier: {
//       // e.g. `import x "foo"`
//     }
//     case '.': {
//       // e.g. `import . "foo"`
//     }
//   }

//   return Err::OK();
// }


Err parse_importDecl(parse& p, Imports& imps) {
  // enter at "import".
  // ImportDecl  = "import" ( ImportSpec | "(" { ImportSpec ";" } ")" )
  // ImportSpec  = [ "." | PackageName ] ImportPath
  // ImportPath  = string_lit

  bool multi = false; // true if in "(" ... ")"
  bool done = false;

  AstNode* pkgName = nullptr;
  defer [&] {
    if (pkgName != nullptr) {
      p.aa->free(pkgName);
    }
  };

  while (1) switch (p.lex.next()) {
    case '(': {
      // e.g. `import ( ... )`
      if (multi) {
        return Err(ParseErrSyntax, "unexpected token");
      }
      multi = true;
      break;
    }

    case ')': {
      if (!multi) {
        return Err(ParseErrSyntax, "unexpected token");
      }
      // expect semicolon or EOF after ")"
      if (!parse_semic(p)) {
        return Err(ParseErrSyntax, "invalid token");
      }
      return Err::OK();
    }

    case '\n': {
      // ignore newline in multi
      if (!multi) {
        return Err(ParseErrSyntax, "unexpected newline");
      }
      break;
    }

    case Lex::TextLit: {
      // e.g. `import "foo";`
      // e.g. `import ( "foo"; );`
      ImportSpecs& s = imps[p.lex.interpretedTokValue()];
      auto res = s.emplace(ImportSpec{pkgName, p.lex.srcLoc()});
      if (!res.second) {
        // duplicate import
        return Err(ParseErrSyntax, "duplicate import");
      }

      // pkgName AstNode taken by ImportSpec above
      pkgName = nullptr;

      // require a semicolon or EOF after ImportSpec
      if (!parse_semic(p)) {
        return Err(ParseErrSyntax, "invalid token");
      }
      if (!multi) {
        return Err::OK();
      }
      break;
    }

    case Lex::Identifier:
    case '.': {
      // e.g. `import x "foo"`
      // e.g. `import . "foo"`
      pkgName = p.allocNode(AstIdent);
      if (p.tokCurr() == '.') {
        static constexpr auto dotStr = ConstIStr(".");
        pkgName->value.str = dotStr;
      } else { // Lex::Identifier
        auto s = p.tokIStr();
        if (lang_isKeyword(s)) {
          return Err("reserved keyword");
        }
        pkgName->value.str = s;
      }
      break;
    }

    case Lex::Error: {
      return p.lex.takeLastError();
    }

    case Lex::End: {
      if (multi) {
        return Err::OK();
      }
      fallthrough;
    }
    default: {
      return Err(ParseErrSyntax, "expected import specification");
    }
  }
}


Err Parser::parsePkgDecl(AstPkgDecl& pkg) {
  // PackageClause = "package" PackageName ";"
  // PackageName   = <Identifier except "_">

  if (_p == nullptr || _p->stage != Stage::Pkg) {
    return Err("invalid parser state");
  }
  auto& p = *_p;
  p.aa = nullptr;

  // "package"?
  switch (p.tokNext(/*acceptEnd=*/true)) {
    case Lex::Error: return p.err;
    case Lex::End: {
      // empty file
      pkg.name = nullptr;
      pkg.doc.clear();
      p.stage = Stage::End;
      return Err::OK();
    }
    case Lex::Identifier: {
      if (p.lex.tokValueCmp("package") == 0) {
        // TODO: set comment, if any
        pkg.doc.clear();
        break;
      }
      fallthrough;
    }
    default: {
      return Err(ParseErrSyntax, "unexpected token; expecting \"package\"");
    }
  }

  // PackageName
  switch (p.tokNext()) {
    case Lex::Error: return p.err;
    case Lex::Identifier: {
      if (p.lex.tokValueCmp("_") == 0) {
        return Err(ParseErr, "invalid package name");
      }
      pkg.name = p.tokIStr();
      if (p.mod.name != nullptr && p.mod.name != pkg.name) {
        return Err(ParseErr, "module name differs from package name");
      } else {
        p.mod.name = pkg.name;
      }
      break;
    }
    default: {
      return Err(ParseErrSyntax, "unexpected token; expecting identifier");
    }
  }

  // ";" or End
  if (!parse_semic(p)) {
    return p.err;
  }

  p.stage = Stage::Import;
  return Err::OK();
}


Err Parser::parseImports(AstAllocator& astalloc, Imports& imps) {
  // Imports     = ImportDecl? | ImportDecl (";" ImportDecl)*
  // ImportDecl  = "import" ( ImportSpec | "(" { ImportSpec ";" } ")" )
  // ImportSpec  = [ "." | PackageName ] ImportPath
  // ImportPath  = string_lit
  // e.g.
  //   import "foo"
  //   import bar "foo"
  //   import . "foo"
  //   import (
  //     "foo"
  //     bar "foo"
  //     . "foo"
  //   )
  //
  if (_p == nullptr || _p->stage != Stage::Import) {
    return Err("invalid parser state");
  }

  auto& p = *_p;
  p.aa = &astalloc;

  auto& lex = p.lex;
  bool done = false;

  while (!done) switch (p.tokNext(/*acceptEnd=*/true)) {
    case Lex::Error: {
      return p.err;
    }
    case Lex::End: {
      done = true;
      break;
    }
    case Lex::Identifier: {
      if (_p->lex.tokValueCmp("import") == 0) {
        auto err = parse_importDecl(*_p, imps);
        if (!err.ok()) {
          return err;
        }
        break;
      }
      fallthrough;
    }
    default: {
      // Something not "import" -- queue token for AST parser.
      p.tokUndo();
      done = true;
      break;
    }
  }

  _p->stage = Stage::AST;
  return Err::OK();
}


Err Parser::parseProgram(AstAllocator& astalloc, AstNode& prog) {
  if (_p == nullptr || _p->stage != Stage::AST) {
    return Err("invalid parser state");
  }

  _p->stage = Stage::End;
  _p->aa = &astalloc;

  while (1) {
    auto node = parse_Declaration(*_p, /*topLevel=*/true);
    if (!node) {
      if (!_p->lex.isValid()) {
        break; // EOF
      }
      assert(!_p->err.ok() ||!"missing error report");
      return _p->err;
    }
    prog.appendChild(*node);
  }

  return Err::OK();
}


const SrcLoc& Parser::srcLoc() const {
  return _p->lex.srcLoc();
}
