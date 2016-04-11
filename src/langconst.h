#pragma once
#include "istr.h"

#define LANG_CONST_KEYWORDS \
  S(type) S(func) \
  S(true) S(false) \
/**/
#define LANG_CONST_INTRINSIC_TYPENAMES \
  S(bool) \
  S(int) \
  S(double) \
/**/
#define LANG_CONST_COMMONS \
  S(a) S(b) S(c) S(d) S(e) S(f) S(g) S(h) S(i) S(j) S(k) S(l) S(m) S(n) S(o) S(p) S(q) S(r) \
  S(s) S(t) S(u) S(v) S(w) S(x) S(y) S(z) \
/**/
#define LANG_CONST_ALL \
  LANG_CONST_KEYWORDS \
  LANG_CONST_INTRINSIC_TYPENAMES \
  LANG_CONST_COMMONS \
/**/

// Returns true if the provided string is a reserved language keyword.
bool lang_isKeyword(const IStr&);

// Symbols -- `IStr::Const kLang_ID` where ID is a name in LANG_CONST_ALL
static constexpr size_t strlen_cx(char const* c) { return *c == '\0' ? 0 : 1 + strlen_cx(c+1); }
#define S(name)  extern const IStr::Const<strlen_cx(#name)+1> kLang_##name;
LANG_CONST_ALL
#undef S
