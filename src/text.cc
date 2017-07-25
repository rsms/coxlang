#include "text.h"

// #if !defined(__EXCEPTIONS) || !__EXCEPTIONS
// #include "utf8/unchecked.h"
  namespace UTF8 = utf8::unchecked;
  //#define UTF8_NEXT(inI, inE) UTF8::next(inI)
  // #define UTF8_NEXT(inI, inE) UTF8::next(inI, inE)
// #else
//   #include "utf8/checked.h"
//   namespace UTF8 = utf8;
//   #define UTF8_NEXT(inI, inE) UTF8::next(inI, inE)
// #endif

#include <iostream>
#include <vector>
using std::cerr;
using std::endl;


namespace text {


Text decodeUTF8(const std::string& s) {
  Text t;
  t.reserve(s.size());
  UTF8::utf8to32(s.cbegin(), s.cend(), std::back_inserter(t));
  return t;
}


string encodeUTF8(const Text& t) {
  std::string s;
  s.reserve(t.size());
  UTF8::utf32to8(t.cbegin(), t.cend(), std::back_inserter(s));
  return s;
}


string encodeUTF8(UChar c) {
  std::string s;
  s.reserve(1);
  UTF8::append(c, std::back_inserter(s));
  return s;
}


bool isGraphicChar(UChar c) {
  // switch (c) {
  //   case '!' ... '~': return true;
  //   default: {
      switch (category(c)) {
        case Category::NormativeLl:
        case Category::NormativeLt:
        case Category::NormativeLu:
        case Category::NormativeNd:
        case Category::NormativeNl:
        case Category::InformativeLo:
        case Category::InformativePc:
        case Category::InformativeSc:
        case Category::InformativeSm:
        case Category::InformativeSo:
        case Category::InformativePd:
        case Category::InformativePe:
        case Category::InformativePf:
        case Category::InformativePi:
        case Category::InformativePs:
        case Category::InformativePo:
          return true;
        default:
          return false;
      }
  //   }
  // }
}


static string escape(UChar c) {
  switch (c) {
    case '\t': return "\\t";
    case '\r': return "\\r";
    case '\n': return "\\n";
    default: {
      string s;
      if (c <= 0xff) {
        s.reserve(5); // \xhh\0
        s.resize(4);
        s.resize(snprintf((char*)s.data(), s.capacity(), "\\x%02x", c));
      } else if (c < 0x10000) {
        s.reserve(7); // \uHHHH\0
        s.resize(6);
        s.resize(snprintf((char*)s.data(), s.capacity(), "\\u%04X", c));
      } else {
        s.reserve(11); // \UHHHHHHHH\0
        s.resize(10);
        s.resize(snprintf((char*)s.data(), s.capacity(), "\\U%08X", c));
      }
      return s;
    }
  }
}


string repr(UChar c) {
  if (isGraphicChar(c)) {
    string s;
    s.reserve(3);
    s += '\'';
    UTF8::append(c, std::back_inserter(s));
    s += '\'';
    return s;
  }
  return escape(c);
}


string repr(const Text& t) {
  std::string s;
  s.reserve(t.size());
  auto start = t.cbegin();
  auto end = t.cend();
  auto I = std::back_inserter(s);

  while (start != end) {
    auto c = *(start++);
    if (c == '\\') {
      *(I++) = '\\';
      *(I++) = '\\';
    } else if (isGraphicChar(c) || isWhitespaceChar(c)) {
      UTF8::append(c, (decltype(I)&)I);
    } else {
      s += escape(c);
    }
  }

  return s;
}


string repr(const char* p, size_t len) {
  auto end = p + len;
  std::string s;
  s.reserve(len);
  auto I = std::back_inserter(s);

  while (p != end) {
    auto c = UTF8::next(p, end);
    if (c == '\\') {
      *(I++) = '\\';
      *(I++) = '\\';
    } else if (isGraphicChar(c) || isWhitespaceChar(c)) {
      UTF8::append(c, (decltype(I)&)I);
    } else {
      s += escape(c);
    }
  }

  return s;
}


#include "text.def"

static Category kCharCategoryMap[RX_TEXT_CHAR_MAP_SIZE] = {
  #define CP(cp, Cat, Bidir, namestr)  (Category)RX_TEXT_CHAR_CAT_##Cat,
  RX_TEXT_CHAR_MAP(CP)
  #undef CP
};


Category category(UChar c) {
  static_assert(RX_TEXT_CHAR_MAP_OFFSET == 0, "Expected Unicode map to start at U+0000");
  if (c < RX_TEXT_CHAR_MAP_SIZE) {
    return kCharCategoryMap[c];
  } else {
    switch (c) {
      #define CP(cp)           case cp:
      #define CR(StartC, EndC) case StartC ... EndC:
      RX_TEXT_INVALID_MAP_ADDITION_RANGES(CP,CR)
      #undef CP
      #undef CR
        return Category::Unassigned;
      default:
        return c <= RX_TEXT_LAST_VALID_CHAR ? Category::Assigned : Category::Unassigned;
    }
  }
}


// Replaced by inlined simple range check
// bool isDecimalDigit(UChar c) {
//   switch (c) {
//     #define CP(cp, name) case cp:
//     RX_TEXT_DECDIGIT_CHARS(CP)
//     #undef CP
//     return true;
//     default: return false;
//   }
// }

// Replaced by inlined simple range checks
// bool isHexDigit(UChar c) {
//   switch (c) {
//     #define CP(cp, name) case cp:
//     RX_TEXT_HEXDIGIT_CHARS(CP)
//     #undef CP
//     return true;
//     default: return false;
//   }
// }

// Replaced by inlined category mapping
// bool isWhitespaceChar(UChar c) {
//   switch (c) {
//     #define CP(cp, name) case cp:
//     RX_TEXT_WHITESPACE_CHARS(CP)
//     #undef CP
//     return true;
//     default: return false;
//   }
// }

bool isLinebreakChar(UChar c) {
  switch (c) {
    #define CP(cp, name) case cp:
    RX_TEXT_LINEBREAK_CHARS(CP)
    #undef CP
    return true;
    default: return false;
  }
}

#ifdef RX_TEXT_CTRL_MAP_ADDITION_RANGES
#warning "RX_TEXT_CTRL_MAP_ADDITION_RANGES is defined but we have disabled isControlChar"
#endif
// Replaced by inlined category mapping
// bool isControlChar(UChar c) {
//   switch (c) {
//     #define CP(cp, name) case cp:
//     RX_TEXT_CTRL_CHARS(CP)
//     #undef CP
//     return true;
//     default: return false;
//   }
// }

UChar caseFold(UChar c) {
  switch (c) {
    #define CP(src, dst, name) case src: return dst;
    RX_TEXT_CASE_FOLDS(CP)
    #undef CP
    default: return c; // Not folded
  }
}



} // namespace text
