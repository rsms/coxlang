#include "parse.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

#define TOK_DEFINE(_) \
  _(space,    ' ') \
  _(line,     '\n') \
  _(int,      'i') \
  _(float,    'f') \
  _(name,     'N') \
  _(package,  'P') \
  _(end,      'E') \
  _(err,      'e')

enum Tok : char {
  #define X(NAME, ch) Tok_##NAME = ch,
  TOK_DEFINE(X)
  #undef X
};

const char* tok_tostr(Tok t) {
  switch (t) {
    #define X(NAME, ch) case Tok_##NAME: return #NAME;
    TOK_DEFINE(X)
    #undef X
    default: return "?";
  }
}

Tok tok_err(Parser& p, const char* errstr) {
  p.errstr = errstr;
  return Tok_err;
}

size_t tok_len(Parser& p) {
  return size_t(p.bufp - p.tokstart);
}

#define SPACE_CASES case ' ': case '\t':
#define LINE_CASES  case '\r': case '\n':
#define DIGIT_CASES \
  case '0': case '1': case '2': case '3': case '4': \
  case '5': case '6': case '7': case '8': case '9':

Tok tok_space(Parser& p) {
  while (p.bufp != p.bufend) switch (*p.bufp) {
    SPACE_CASES {
      ++p.bufp;
      break;
    }
    default: {
      return Tok_space;
    }
  }
  return Tok_space;
}

Tok tok_identifier(Parser& p) {
  // letter        = unicode_letter | "_"
  // identifier    = letter { letter | unicode_digit }

  while (p.bufp != p.bufend) {
    char c = *p.bufp;
    if (c > 'A')
    switch (*p.bufp) {
    SPACE_CASES
    LINE_CASES
    case ':': case '.': case ';':
    case '<': case '>': case '[': case ']': case '(': case ')':
    case ':':
    {
      goto ret;
    }
    default: {
      ++p.bufp;
    }
  }
  ret:
  if (tok_len(p) == 7 &&
      memcmp((const void*)p.tokstart, (const void*)"__END__", 7) == 0)
  {
    return Tok_end;
  }
  return Tok_name;
}

Tok tok_float(Parser& p) {
  assert(*(p.bufp-1) == '.');
  bool hasExp = false;
  top:
  while (p.bufp != p.bufend) switch (*p.bufp) {
    DIGIT_CASES {
      ++p.bufp;
      break;
    }
    case '.': {
      // else leave '.' in buffer to allow `123.4.foo()`
      return Tok_float;
    }
    case 'e': case 'E': {
      // ("e"|"E") ["+"|"-"] digit+
      ++p.bufp;
      if (!hasExp && p.bufp != p.bufend) {
        hasExp = true;
        if (*p.bufp == '+' || *p.bufp == '-') {
          ++p.bufp;
        }
        if (p.bufp != p.bufend) {
          switch (*p.bufp) {
            DIGIT_CASES { ++p.bufp; goto top; }
            default: break;
          }
        }
      }
      return tok_err(p, "invalid number literal");
    }
    default: return Tok_float;
  }
  return Tok_float;
}

Tok tok_int(Parser& p) {
  char kind = '0';
  while (p.bufp != p.bufend) switch (*p.bufp) {
    case '.': {
      if (kind == '0') {
        ++p.bufp;
        return tok_float(p);
      }
      // else leave '.' in buffer to allow `123.foo()`
      return Tok_int;
    }
    case 'x': case 'X': {
      if (*(p.bufp-1) != '0') {
        // not `0x...`
        return tok_err(p, "invalid number literal");
      }
      kind = 'x';
      ++p.bufp;
      break;
    }
    DIGIT_CASES {
      ++p.bufp;
      break;
    }
    default: return Tok_int;
  }
  return Tok_int;
}

Tok tok_dotsomething(Parser& p) {
  if (p.bufp == p.bufend) {
    return tok_err(p, "unexpected token");
  }
  switch (*p.bufp) {
    DIGIT_CASES return tok_float(p);
    default:    return Tok('.');
  }
}

Tok tok_next(Parser& p) {
  p.tokstart = p.bufp;
  switch (*p.bufp++) {
    SPACE_CASES {
      return tok_space(p);
    }
    case '\r': {
      if (p.bufp == p.bufend || *p.bufp != '\n') {
        return tok_err(p, "unexpected \\r");
      }
      ++p.bufp;
      return Tok_line;
    }
    case '\n': {
      return Tok_line;
    }
    
    DIGIT_CASES {
      return tok_int(p);
    }
    
    case '.': {
      return tok_dotsomething(p);
    }

    default: {
      --p.bufp;
      return tok_identifier(p);
    }
  }
}


ParseStatus parse(Parser& p, const char* s, size_t len) {

  p.bufstart = s;
  p.bufp = s;
  p.bufend = s + len;
  p.tokstart = nullptr;

  while (p.bufp != p.bufend) {
    Tok t = tok_next(p);

    printf("tok_next => %s \"%.*s\"\n",
      tok_tostr(t), int(tok_len(p)), p.tokstart);
    
    if (t == Tok_err) {
      printf("err: %s\n", p.errstr);
      return ParseErr;
    }
    if (t == Tok_end) {
      break;
    }
  }

  return ParseOK;
}
