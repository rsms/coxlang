#include "lex.h"
#include <iostream>
#include <stack>
#include <deque>
#include <assert.h>

#if !defined(__EXCEPTIONS) || !__EXCEPTIONS
  #include "utf8/unchecked.h"
  namespace UTF8 = utf8::unchecked;
#else
  #include "utf8/checked.h"
  namespace UTF8 = utf8;
#endif


#if __clang__
  #define fallthrough [[clang::fallthrough]]
#else
  #define fallthrough (void(0))
#endif


using std::cerr;
using std::endl;
#define DBG(...) cerr << "[" << __BASE_FILE__ << "] " <<  __VA_ARGS__ << endl;
// #define DBG(...)

#define FOREACH_CHAR \
  while (_p != _end) switch (nextChar())


#include "text.def"

#define LINEBREAK_CP(cp, name) case cp:
#define LINEBREAK_CASES  RX_TEXT_LINEBREAK_CHARS(LINEBREAK_CP)

#define WHITESPACE_CP(cp, name) case cp:
#define WHITESPACE_CASES  RX_TEXT_WHITESPACE_CHARS(WHITESPACE_CP)

#define CTRL_CP(cp, name) case cp:
#define CTRL_CASES  RX_TEXT_CTRL_CHARS(CTRL_CP)

#define OCTNUM_CP(cp, name) case cp:
#define OCTNUM_CASES  RX_TEXT_OCTDIGIT_CHARS(OCTNUM_CP)

#define DECNUM_CP(cp, name) case cp:
#define DECNUM_CASES  RX_TEXT_DECDIGIT_CHARS(DECNUM_CP)

#define HEXNUM_CP(cp, name) case cp:
#define HEXNUM_CASES  RX_TEXT_HEXDIGIT_CHARS(HEXNUM_CP)

#define XXXXXSUBLIMETEXTWTFS

using Token = Lex::Token;

struct TokQueue {
  struct Entry {
    Token  tok;
    SrcLoc loc;
    Text   val;
  };

  std::deque<Entry> _q;

  const Entry& first() const { return _q.front(); }
  const Entry& last() const { return _q.back(); }
  size_t size() const { return _q.size(); }
  bool empty() const { return _q.empty(); }

  // add to end of queue
  void enqueueLast(Token tok, const SrcLoc& loc, const Text& val) {
    _q.emplace_back(Entry{tok, loc, val});
  }

  // add to beginning of queue
  void enqueueFirst(Token tok, const SrcLoc& loc, const Text& val) {
    _q.emplace_front(Entry{tok, loc, val});
  }

  // dequeue the token+srcloc that's first in line
  void dequeueFirst(Token& tok, SrcLoc& loc, Text& val) {
    auto& ent = _q.front();
    tok = ent.tok;
    loc = std::move(ent.loc);
    val = std::move(ent.val);
    _q.pop_front();
  }
};

struct Lex::Imp {
  struct StackFrame {
    Token t;
    StackFrame(Token t) : t{t} {}
  };
  using Stack = std::stack<StackFrame>;

  const char* _begin;
  const char* _end;
  const char* _p;
  UChar       _c;  // current character
  Token       _tok;
  TokQueue    _tokQueue;
  uint32_t    _interpolatedTextDepth = 0;
  const char* _lineBegin;
  SrcLoc      _srcLoc;
  Err         _err;
  Stack       _stack;

  Imp(const char* p, size_t z)
    : _begin{p}
    , _end{p+z}
    , _p{p}
    , _tok{Tokens::End}
    , _lineBegin{p}
  {}


  void undoChar() {
    _p -= text::UTF8SizeOf(_c);
  }

  UChar nextChar() {
    return _c = _p < _end ? UTF8::next(_p, _end) : UCharMax;
  }

  UChar peekNextChar() {
    auto p = _p;
    return p < _end ? UTF8::next(p, _end) : UCharMax;
  }

  // UChar peekNextNonSpaceChar() {
  //   auto p = _p;
  //   UChar c;
  //   while (p != _end) switch ((c = UTF8::next(p, _end))) {
  //     CTRL_CASES
  //     WHITESPACE_CASES
  //       break;
  //     default:
  //       return c;
  //   }
  //   return UCharMax; // End
  // }

  // Scans ahead without changing state, and
  // if the first non-whitespace-or-control is predicatech,
  // advance lexer to that position and return true.
  // Returns false if the first non-whitespace-or-control is not predicatech.
  // When this function returns true, the next call to Lex::next() will
  // read the token _after_ predicatech (_not including_ predicatech.)
  // bool nextCharIf(UChar predicatech) {
  //   auto p = _p;
  //   UChar c;
  //   SrcLoc loc = _srcLoc;
  //   loc.offset = _p - _begin;
  //   loc.column = _p - _lineBegin;
  //   while (p != _end) switch ((c = UTF8::next(p, _end))) {
  //     CTRL_CASES
  //     WHITESPACE_CASES
  //       break;
  //     default: {
  //       if (c == predicatech) {
  //         _srcLoc = loc;
  //         _p = p; // advance read position
  //         _c = c; // set current char
  //         setTok(c);
  //         return true;
  //       }
  //       return false;
  //     }
  //   }
  //   return false; // End
  // }

  void updateSrcLocLength(Token t, SrcLoc& loc) {
    // Note: Must be possible to apply this function to the same loc and _ state
    // multiple times with the same results.
    if (t == End) {
      loc.length = 0;
    } else {
      loc.length = (_p - _begin) - loc.offset;
    }
  }

  Token setTok(Token t) {
    updateSrcLocLength(t, _srcLoc);
    return _tok = t;
  }

  void enqueueToken(Token t, const Text& value) {
    updateSrcLocLength(t, _srcLoc);
    _tokQueue.enqueueLast(t, _srcLoc, value);
  }

  void enqueueTokenFirst(Token t, const Text& value) {
    updateSrcLocLength(t, _srcLoc);
    _tokQueue.enqueueFirst(t, _srcLoc, value);
  }

  void beginTok() {
    _srcLoc.offset = _p - _begin;
    _srcLoc.column = _p - _lineBegin;
  }

  void incrLine() {
    _srcLoc.line++;
    _lineBegin = _p;
  }

  Token error(const string& msg) {
    _err = {msg};
    return setTok(Error);
  }


  bool shouldInsertSemicolon() {
    return _tok == Identifier ||
           (_tok > BeginLit && _tok < EndLit) ||
           _tok == U')' ||
           _tok == U']' ||
           _tok == U'}' ;
  }

  // Advance lexer only if the upcoming token is `pred`
  // bool nextIfEq(Token pred, Text& value) {
  //   // copy state
  //   auto p = _p;
  //   auto c = _c;
  //   auto tok = _tok;
  //   auto lineBegin = _lineBegin;
  //   auto srcLoc = _srcLoc;

  //   // read next
  //   if (next(value) == pred) {
  //     return true;
  //   }

  //   // restore state
  //   _p = p;
  //   _c = c;
  //   _tok = tok;
  //   _lineBegin = lineBegin;
  //   _srcLoc = srcLoc;
  //   return false;
  // }

  Token next(Text& value) {
    if (_tok == '\n') {
      // Increase line if previous token was a linebreak. This way linebreaks are
      // semantically at the end of a line, rather than at the beginning of a line.
      incrLine();
    }

    // First, return the next queued token before reading more
    if (!_tokQueue.empty()) {
      _tokQueue.dequeueFirst(_tok, _srcLoc, value);
      return _tok;
    }

    beginTok();
    value.clear();
      // Set source location and clear value

    // The root switch has a dual purpose: Initiate tokens and reading symbols.
    // Because symbols are pretty much "anything else", this is the most
    // straight-forward way.
    bool isReadingIdent = false;
    #define ADDSYM_OR \
      if (isReadingIdent) { value += _c; break; } else
    #define ENDSYM_OR \
      if (isReadingIdent) { undoChar(); return setTok(Identifier); } else

    FOREACH_CHAR {
      CTRL_CASES  WHITESPACE_CASES  ENDSYM_OR { beginTok(); break; } // ignore

      case '\n': ENDSYM_OR {
        // When the input is broken into tokens, a semicolon is automatically
        // inserted into the token stream at the end of a non-blank line if the
        // line's final token is
        //   • an identifier
        //   • an integer, floating-point, imaginary, rune, or string literal
        //   • one of the keywords break, continue, fallthrough, or return
        //   • one of the operators and delimiters ++, --, ), ], or }
        if (shouldInsertSemicolon()) {
          enqueueToken('\n', value);
          return setTok(';');
        } else {
          return setTok('\n');
        }
      }

      case '(': ENDSYM_OR {
        _stack.emplace('(');
        return setTok(_c);
      }

      case ')': ENDSYM_OR {
        if (_stack.empty()) {
          return error("unbalanced parenthesis");
        }
        auto popTok = _stack.top().t;
        _stack.pop();
        switch (popTok) {
          case ITextLit:  return readTextLit(value, /*isInterpolated=*/true);
          case '(':       return setTok(_c);
          default:        assert(!"invalid stack state"); break;
        }
      }

      case '{': case '}':
      case '[': case ']':
      case '<': case '>':
      case ',': case ';':
      case '+': case '*':
      case '!':
      case '=':
      // TODO: other operators, like &
        ENDSYM_OR return setTok(_c);

      case ':':  ENDSYM_OR return readColonOrAutoAssign(value);
      case '-':  ENDSYM_OR return readMinusOrRArr(value);
      case '/':  ENDSYM_OR return readSolidus(value);
      case '.':  ENDSYM_OR return readDot(value); // "." | ".." | "..." | "."<float>
      case U'\u2702':   ENDSYM_OR return readDataTail(); // "✂" [^\n]* \n

      case '0':         ADDSYM_OR return readZeroLeadingNumLit(value);
      case '1' ... '9': ADDSYM_OR return readDecIntLit(value);

      case '\'':  return readCharLit(value);
      case '"':   return readTextLit(value, /*isInterpolated=*/false);

      default: {
        if (text::isValidChar(_c)) {
          isReadingIdent = true;
          value += _c;
        } else {
          return error("Illegal character "+text::repr(_c)+" in input");
        }
        break;
      }
    }

    return setTok(isReadingIdent ? Identifier : End);
  }


  Token readColonOrAutoAssign(Text&) {
    // ColonOrAutoAssign = ":" | AutoAssign
    // AutoAssign        = ":="
    switch (nextChar()) {
      case UCharMax:        return error("Unexpected end of input");
      case '=':             return setTok(AutoAssign);
      default:  undoChar(); return setTok(U':');
    }
  }


  Token readMinusOrRArr(Text&) {
    // MinusOrRArr = "-" | RArr
    // RArr        = "->"
    switch (nextChar()) {
      case UCharMax:        return error("Unexpected end of input");
      case '>':             return setTok(RArr);
      default:  undoChar(); return setTok(U'-');
    }
  }


  // Token readEq(Text&) {
  //   // Eq   = "=" | EqEq
  //   // EqEq = "=="
  //   switch (nextChar()) {
  //     case UCharMax:        return error("Unexpected end of input");
  //     case '=':             return setTok(EqEq);
  //     default:  undoChar(); return setTok(U'=');
  //   }
  // }


  Token readSolidus(Text& value) {
    // Solidus        = "/" | LineComment | GeneralComment
    // LineComment    = "//" <any except LF> <LF>
    // GeneralComment = "/*" <any> "*/"
    switch (peekNextChar()) {
      case UCharMax: return error("Unexpected end of input");

      case '/':
      case '*': {
        nextChar();
        // A general comment containing no newlines acts like a space.
        // Any other comment acts like a newline.
        bool insertSemic = shouldInsertSemicolon();

        if (_c == '/') {
          readLineComment(value);
        } else {
          bool hasNewline;
          if (readGeneralComment(value, hasNewline) == Error) {
            return _tok;
          }
          if (insertSemic) {
            // if we should insert a semicolon before the comment,
            // only do so if the general comment contains a newline.
            insertSemic = hasNewline;
          }
        }

        if (insertSemic) {
          enqueueToken(_tok, value);
          return setTok(';');
        }

        return _tok;
      }

      default:  return setTok('/');
    }
  }


  Token readGeneralComment(Text& value, bool& hasNewline) {
    // Enter at "/*", leave at "*/"
    hasNewline = false;
    FOREACH_CHAR {
      case '\n': {
        value += _c;
        hasNewline = true;
        incrLine();
        break;
      }
      case '*': {
        if (peekNextChar() == '/') {
          nextChar(); // eat '/'
          return setTok(GeneralComment);
        }
        fallthrough;
      }
      default: {
        assert(_c != UCharMax); // FOREACH_CHAR should protect against this case
        value += _c;
        break;
      }
    }
    return error("unterminated general comment");
  }


  Token readLineComment(Text& value) {
    // Enter at "//"
    FOREACH_CHAR {
      case '\n': undoChar(); return setTok(LineComment);
      default:   value += _c; break;
    }
    return setTok(LineComment);
  }


  Token readDataTail() {
    // Enter at <end marker char> and discard everything up until and
    // including <LF>, then "return" the remainder of the source as a data tail
    while (nextChar() != '\n') {
      if (_c == UCharMax) { beginTok(); return setTok(End); }
    }
    incrLine();
    beginTok();
    _p = _end;
    _lineBegin = _p;
    return setTok(DataTail);
  }


  // bool readTextInterpolation()


  template <UChar TermC>
  bool readCharLitEscape(Text& value) {
    // EscapedUnicodeChar  = "\n" | "\r" | "\t" | "\\" | <TermC>
    //                       | LittleXUnicodeValue
    //                       | LittleUUnicodeValue
    //                       | BigUUnicodeValue
    // LittleXUnicodeValue = "\x" HexDigit HexDigit
    // LittleUUnicodeValue = "\u" HexDigit HexDigit HexDigit HexDigit
    // BigUUnicodeValue    = "\U" HexDigit HexDigit HexDigit HexDigit
    //                            HexDigit HexDigit HexDigit HexDigit
    switch (nextChar()) {
      case 'n': { value += '\n'; break; }
      case 'r': { value += '\r'; break; }
      case 't': { value += '\t'; break; }
      case TermC:
      case '\\': { value += _c; break; }
      case 'x': { return readHexUChar(2, value); }
      case 'u': { return readHexUChar(4, value); }
      case 'U': { return readHexUChar(8, value); }
      default: {
        error(
          "Unexpected escape sequence '\\' "+
          text::repr(_c)+
          " in character literal"
        );
        return false;
      }
    }
    return true;
  }


  Token readCharLit(Text& value) {
    // CharLit = "'" ( UnicodeChar | EscapedUnicodeChar<'> ) "'"
    switch (nextChar()) {
      case UCharMax:
        return error("Unterminated character literal at end of input");
      LINEBREAK_CASES
        return error("Illegal character in character literal");
      case '\'':
        return error("Empty character literal or unescaped ' in character literal");
      case '\\':
        if (!readCharLitEscape<'\''>(value)) return _tok; break;
      default:
        value = _c; break;
    }
    switch (nextChar()) {
      case '\'':      return setTok(CharLit);
      default:        return error("Expected ' to end single-character literal");
    }
  }


  Token readTextLit(Text& value, bool isInterpolated) {
    // TextLit = '"' ( UnicodeChar | EscapedUnicodeChar<"> )* '"'
    FOREACH_CHAR {
      LINEBREAK_CASES return error("Illegal character in character literal");
      case '"': {
        if (isInterpolated) {
          return setTok(ITextLitEnd);
        } else {
          return setTok(TextLit);
        }
      }
      case '\\': {
        if (peekNextChar() == '(') {
          undoChar();
          setTok(ITextLit);
          _stack.emplace(_tok);
          _p += 2; // skip past the "\(" which we previously parsed
          return _tok;
        }
        if (!readCharLitEscape<'"'>(value)) return _tok;
        break;
      }
      default:        value += _c; break;
    }
    return error("Unterminated character literal at end of input");
  }


  bool readHexUChar(int nbytes, Text& value) {
    string s;
    s.reserve(nbytes);
    int i = 0;
    while (i++ != nbytes) {
      switch (nextChar()) {
        case '0' ... '9': case 'A' ... 'F': case 'a' ... 'f': {
          s += (char)_c; break;
        }
        default: {
          error("Invalid Unicode sequence");
          return false;
        }
      }
    }
    UChar c = std::stoul(s, 0, 16);
    switch (text::category(c)) {
      case text::Category::Unassigned:
      case text::Category::NormativeCs: {
        error(
          string{"Invalid Unicode character \\"} + (nbytes == 4 ? "u" : "U") + s
        );
        return false;
      }
      default: break;
    }
    value += c;
    return true;
  }


  // === Integer literals ===
  // int_lit     = decimal_lit | octal_lit | hex_lit
  // decimal_lit = ("0" | ( "1" … "9" ) { decimal_digit })
  // octal_lit   = "0" { octal_digit }
  // hex_lit     = "0" ( "x" | "X" ) hex_digit { hex_digit }
  //

  Token readZeroLeadingNumLit(Text& value) {
    value = _c; // enter at _c='0'
    FOREACH_CHAR {
      case 'X': case 'x': return readHexIntLit(value);
      case '.':           return readFloatLitAtDot(value);
      OCTNUM_CASES        return readOctIntLit(value);
      default: {
        undoChar();
        return setTok(DecIntLit); // special case '0'
      }
    }
    return setTok(DecIntLit); // special case '0' as last char in input
  }


  Token readOctIntLit(Text& value) {
    value = _c; // Enter at ('1' ... '7')
    FOREACH_CHAR {
      OCTNUM_CASES { value += _c; break; }
      case '.': {
        value.insert(0, 1, '0');
        return readFloatLitAtDot(value);
      }
      default: { undoChar(); return setTok(OctIntLit); }
    }
    return setTok(OctIntLit);
  }


  Token readDecIntLit(Text& value) {
    value = _c; // Enter at ('1' ... '9')
    FOREACH_CHAR {
      DECNUM_CASES { value += _c; break; }
      case 'e': case 'E':    return readFloatLitAtExp(value);
      case '.':              return readFloatLitAtDot(value);
      default: { undoChar(); return setTok(DecIntLit); }
    }
    return setTok(DecIntLit);
  }


  Token readHexIntLit(Text& value) {
    value.clear(); // ditch '0'. Enter at "x" in "0xN..."
    FOREACH_CHAR {
      HEXNUM_CASES { value += _c; break; }
      default: { undoChar(); return setTok(HexIntLit); }
    }
    return error("Incomplete hex literal"); // special case: '0x' is last of input
  }


  Token readDot(Text& value) {
    value = _c; // enter at "."
    switch (nextChar()) {
      case UCharMax: return error("Unexpected '.' at end of input");
      case U'.': {
        // Dots      = DotDot | DotDotDot
        // DotDot   = ".."
        // DotDotDot = "..."
        if (nextChar() == U'.') {
          if (nextChar() == U'.') {
            return error("Unexpected '.' after '...'");
          } else {
            undoChar();
            return setTok(DotDotDot);
          }
        } else {
          undoChar();
          return setTok(DotDot);
        }
      }
      DECNUM_CASES         return readFloatLitAtDot(value);
      default: undoChar(); return setTok(U'.');
    }
  }


  Token readFloatLitAtDot(Text& value) {
    value += _c; // else enter at "<decnum>."
    // float_lit = decimals "." [ decimals ] [ exponent ] |
    //             decimals exponent |
    //             "." decimals [ exponent ]
    // decimals  = decimal_digit { decimal_digit }
    // exponent  = ( "e" | "E" ) [ "+" | "-" ] decimals
    FOREACH_CHAR {
      DECNUM_CASES { value += _c; break; }
      case 'e': case 'E':    return readFloatLitAtExp(value);
      default: { undoChar(); return setTok(FloatLit); }
    }
    return setTok(FloatLit); // special case '0.'
  }


  Token readFloatLitAtExp(Text& value) {
    value += _c; // enter at "<decnum>[E|e]"
    if (_p == _end) return error("Incomplete float exponent");
    switch (nextChar()) {
      case '+': case '-': { value += _c; break; }
      DECNUM_CASES { value += _c; break; }
      default: goto illegal_exp_value;
    }

    if (_c == '+' || _c == '-') {
      if (_p == _end) return error("Incomplete float exponent");
      switch (nextChar()) {
        DECNUM_CASES { value += _c; break; }
        default: { goto illegal_exp_value; }
      }
    }

    FOREACH_CHAR {
      DECNUM_CASES { value += _c; break; }
      default: { undoChar(); return setTok(FloatLit); }
    }

    return setTok(FloatLit); // special case '<decnum>[E|e]?[+|-]?<decnum>'

   illegal_exp_value:
    error("Illegal value '" + text::repr(_c) + "' for exponent in float literal");
    undoChar();
    return _tok;
  }


};


Lex::Lex(const char* p, size_t z) : self{new Imp{p, z}} {}
Lex::~Lex() { if (self != nullptr) delete self; }

bool Lex::isValid() const {
  return !self->_tokQueue.empty() ||
         self->_p < self->_end;
}
const Err& Lex::lastError() const { return self->_err; }
Err&& Lex::takeLastError() { return std::move(self->_err); }

Token Lex::next(Text& value) {
  return self->next(value);
}
// bool Lex::nextIfEq(Token pred, Text& value) {
//   return self->nextIfEq(pred, value);
// }
Token Lex::current() const {
  return self->_tok;
}
void Lex::undoCurrent(const Text& value) {
  self->enqueueTokenFirst(self->_tok, value);
}
Token Lex::queuedToken() {
  return self->_tokQueue.empty() ? Lex::Error : self->_tokQueue.first().tok;
}

// bool Lex::nextCharIf(UChar predicatech) {
//   return self->nextCharIf(predicatech);
// }
// UChar Lex::peekNextChar() {
//   return self->peekNextNonSpaceChar();
// }

const SrcLoc& Lex::srcLoc() const { return self->_srcLoc; }

const char* Lex::byteTokValue(size_t& z) const {
  z = self->_srcLoc.length;
  return self->_begin + self->_srcLoc.offset;
}

void Lex::copyTokValue(string& s) const {
  s.assign(self->_begin + self->_srcLoc.offset, self->_srcLoc.length);
}

string Lex::byteStringTokValue() const {
  return {self->_begin + self->_srcLoc.offset, self->_srcLoc.length};
}

string Lex::repr(Token t, const Text& value) {
  switch (t) {
    #define T(Name, HasValue) case Lex::Tokens::Name: { \
      return (HasValue && !value.empty()) ? \
        string{#Name} + " \"" + text::repr(value) + "\"" : \
        string{#Name}; \
    }
    RX_LEX_TOKENS(T)
    #undef T
    default:
      return text::repr(t);
  }
}

Lex::Snapshot Lex::createSnapshot() {
  return Snapshot{new Imp{*self}};
}

void Lex::restoreSnapshot(const Lex::Snapshot& snapshot) {
  *self = *snapshot.self;
}

void Lex::swapSnapshot(Lex::Snapshot& snapshot) {
  auto* s = self;
  self = snapshot.self;
  snapshot.self = s;
}

Lex::Snapshot::~Snapshot() {
  if (self) delete self;
}
