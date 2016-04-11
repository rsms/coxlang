#include "langconst.h"

#define S(name)  constexpr auto kLang_##name = ConstIStr(#name);
LANG_CONST_ALL
#undef S

bool lang_isKeyword(const IStr& s) {
  // Let's rely on a switch until we have our first collision,
  // in which case the compiler will tell us.
  switch (s.hash()) {
    #define S(name) case kLang_##name.hash():
    LANG_CONST_KEYWORDS
    #undef S
      return true;
    default:
      return false;
  }
}
