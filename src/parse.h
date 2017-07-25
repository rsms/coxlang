#pragma once
#include "error.h"
#include "srcloc.h"
#include "imp.h"
#include "ast.h"
#include "istr.h"
#include "mod.h"
#include <stdlib.h>

// opaque parser implementation data
struct parse;

// Error codes
enum ParseErrCode : Err::Code {
  ParseErr,
  ParseErrSyntax,
};

// Parser allows partially or completely parsing a translation unit
struct Parser {
  // Construct a parser that will parse source code at sp of len bytes
  Parser(const char* sp, size_t len, IStr::WeakSet&, Module&);

  // Parse source in the following sequence:
  Err parsePkgDecl(AstPkgDecl& pkgdecl);      // parse package declaration, then
  Err parseImports(AstAllocator&, Imports&);  // parse any import declarations, then
  Err parseProgram(AstAllocator&, AstNode&);  // parse any program.

  // TODO: Flag to parseProgram that includes comments in the AST

  // Current location in source. Useful when an error occurs.
  const SrcLoc& srcLoc() const;

  Parser(): _p{nullptr} {} // invalid parser, useful as a placeholder
  Parser(Parser&&) = default; // movable
  ~Parser();

private:
  friend struct parse;
  parse* _p;
  Parser(const Parser&) = delete; // not copyable as Lex doesn't support copy
};
