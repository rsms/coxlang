#include "parse.h"
#include "readfile.h"
#include "wasm.h"
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
  // Read input from stdin or a file
  FILE* f = stdin;
  if ((argc > 1) && !(f = fopen(argv[1], "r"))) {
    err(1, "%s", argv[0]);
  }
  size_t srcz = 0;
  char* srcp = readfile(f, srcz, 102400000);
  if (!srcp) {
    err(1, "%s", argv[0]);
  }
  fclose(f); f = nullptr;

  // We use these for the entire thread memory space
  IStr::WeakSet strings;  // string interning
  AstAllocator  astalloc; // AST allocator

  // We use this for the entire module
  Module module;

  // We use these for one translation unit
  Err    error;
  Parser p(srcp, srcz, strings, module);

  // package
  AstPkgDecl pkgdecl;
  error = p.parsePkgDecl(pkgdecl);
  if (!error.ok()) {
    reportParseErr(p, error, srcp, srcz);
  }
  cout << "package: " << pkgdecl.name << endl;
  if (!pkgdecl.doc.empty()) {
    cout << pkgdecl.doc << endl;
  }

  // imports
  Imports imps;
  error = p.parseImports(astalloc, imps);
  if (!error.ok()) {
    reportParseErr(p, error, srcp, srcz);
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
  error = p.parseProgram(astalloc, *prog);
  if (!error.ok()) {
    reportParseErr(p, error, srcp, srcz);
  }
  ast_repr(*prog, cout) << endl;

  // WASM codegen
  wasm::Buf wbuf;
  error = wasm::emit_module(wbuf, *prog);
  if (!error.ok()) {
    cerr << "genwasm: " << error.message() << endl;
    abort();
  }

  // Write output
  if (argc > 2) {
    FILE* of = fopen(argv[2], "w");
    if (of == nullptr) {
      err(1, "%s", argv[0]);
    }
    printf("write WASM code to %s\n", argv[2]);
    if (fwrite((const void*)wbuf.data(), wbuf.size(), 1, of) == 0) {
      err(1, "%s", argv[0]);
    }
    fclose(f); f = nullptr;
  }


  astalloc.free(prog);
  free(srcp);

  return 0;
}