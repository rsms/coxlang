#pragma once
#include "error.h"
#include "srcloc.h"
#include "text.h"
#include "pkg.h"
#include "imp.h"
#include "ast.h"
#include "istr.h"
#include <stdlib.h>

enum ParseErrCode : Err::Code {
  ParseErr,
  ParseErrSyntax,
};

struct parse;

struct Parser {
  Parser(const char* sp, size_t len, IStr::WeakSet&);
  Parser();
  Parser(Parser&&) = default; // movable
  ~Parser();

  // Parse source in the following sequence:
  Err parsePkg(PkgDef& pkg);                  // parse package statement, then
  Err parseImports(AstAllocator&, Imports&);  // parse any import declarations, then
  Err parseProgram(AstAllocator&, AstNode&);  // parse any program.

  // Current location in source. Useful when an error occurs.
  const SrcLoc& srcLoc() const;

private:
  friend struct parse;
  parse* _p;
  Parser(const Parser&) = delete; // not copyable as Lex doesn't support copy
};

inline Parser::Parser() : _p{nullptr} {}
