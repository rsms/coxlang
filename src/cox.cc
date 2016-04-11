#include "parse.h"
#include "readfile.h"
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <err.h>
#include <iostream>

using std::cout;
using std::cerr;
using std::endl;
using std::string;

string getSrcCtx(const char* srcp, size_t srcz, const SrcLoc& loc, size_t n) {
  const char* endp = srcp + srcz;
  const char* p = srcp + loc.offset;
  const char* ctxbeginaddlp = srcp;   // beginning of additional lines
  const char* ctxbeginp = srcp;       // beginning of interesting line
  const char* ctxendp = endp;

  // find beginning of line
  size_t nlines = n;
  while (p >= srcp) {
    if (*p == '\n') {
      if (ctxbeginp == srcp) {
        ctxbeginaddlp = ctxbeginp = p+1;
      }
      if (nlines == 0) {
        ctxbeginaddlp = p+1;
        break;
      }
      --nlines;
    }
    --p;
  }

  // find end of interesting line
  p = (srcp + loc.offset + (loc.length == 0 ? 0 : loc.length-1));
  while (p < endp) {
    if (*p == '\n') {
      ctxendp = p;
      break;
    }
    ++p;
  }

  // join additional "before" lines + interesting line
  string s{ctxbeginaddlp, size_t(ctxendp - ctxbeginaddlp)};

  // append "pointer" (i.e. ~~~^)
  s += "\n";
  for (size_t col = 0; col != loc.column; ++col) {
    s += ' ';
  }
  if (loc.length > 1) {
    for (size_t i = 0; i != loc.length; ++i) {
      s += '~';
    }
  } else {
    s += '^';
  }

  return s;
}


void reportParseErr(Parser& p, const Err& err, const char* srcp, size_t srcz) {
  cerr << "parse error: " << err.message();
  if (err.code() == ParseErrSyntax) {
    auto& loc = p.srcLoc();
    cerr << " at " << (loc.line+1) << ":" << (loc.column+1);
    if (srcp != nullptr) {
      auto sctx = getSrcCtx(srcp, srcz, loc, 1);
      cerr << "\n" << sctx;
    }
  }
  cerr << endl;
  exit(1);
}


int main(int argc, char const *argv[]) {

  FILE* f = stdin;
  if ((argc > 1) && !(f = fopen(argv[1], "r"))) {
    err(1, "%s", argv[0]);
  }

  size_t srcz = 0;
  char* srcp = readfile(f, srcz, 102400000);
  if (!srcp) {
    err(1, "%s", argv[0]);
  }

  IStr::WeakSet strings;
  AstAllocator astalloc;
  Parser p(srcp, srcz, strings);
  Err err;

  // package
  PkgDef pkg;
  err = p.parsePkg(pkg);
  if (!err.ok()) {
    reportParseErr(p, err, srcp, srcz);
  }
  cout << "package: " << pkg.name << endl;
  if (!pkg.doc.empty()) {
    cout << pkg.doc << endl;
  }

  // imports
  Imports imps;
  err = p.parseImports(astalloc, imps);
  if (!err.ok()) {
    reportParseErr(p, err, srcp, srcz);
  }
  if (imps.empty()) {
    cout << "no imports" << endl;
  } else {
    cout << "imports: " << endl;
    for (auto& e : imps) {
      cout << "  \"" << e.first << "\"";
      size_t n = 0;
      for (auto& imp : e.second) {
        cout << (++n == 1 ? " as " : ", ");
        if (imp.name == nullptr) {
          cout << "?";
        } else {
          cout << imp.name->value.str; // TODO: ast_repr
        }
      }
      cout << endl;
    }
  }

  // AST
  auto prog = astalloc.alloc();
  prog->type = AstProgram;
  err = p.parseProgram(astalloc, *prog);
  if (!err.ok()) {
    reportParseErr(p, err, srcp, srcz);
  }
  ast_repr(*prog, cout) << endl;

  astalloc.free(prog);
  free(srcp);

  return 0;
}