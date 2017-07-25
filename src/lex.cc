#include "lex.h"
#include "strtoint.h"
#include <iostream>
#include <stack>
#include <deque>
#include <assert.h>

// When defined, unicode codepoints are validated:
// #define VALIDATE_UNICODE

#if __clang__
  #define fallthrough [[clang::fallthrough]]
#else
  #define fallthrough (void(0))
#endif

using std::string;
using std::cerr;
using std::endl;
#define DBG(...) cerr << "[" << __BASE_FILE__ << "] " <<  __VA_ARGS__ << endl;
// #define DBG(...)

#define FOREACH_CHAR \
  while (_p != _end) switch (nextChar())

#define FOREACH_BYTE \
  while (_p != _end) switch (nextByte())

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
    string val;
  };

  std::deque<Entry> _q;

  const Entry& first() const { return _q.front(); }
  const Entry& last() const { return _q.back(); }
  size_t size() const { return _q.size(); }
  bool empty() const { return _q.empty(); }

  // add to end of queue
  void enqueueLast(Token tok, const SrcLoc& loc, const string& val) {
    _q.emplace_back(Entry{tok, loc, val});
  }

  // add to beginning of queue
  void enqueueFirst(Token tok, const SrcLoc& loc, const string& val) {
    _q.emplace_front(Entry{tok, loc, val});
  }

  // dequeue the token+srcloc that's first in line
  void dequeueFirst(Token& tok, SrcLoc& loc, string& val) {
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
  string      _strval; // value of interpreted literals (string and char)

  Imp(const char* p, size_t z)
    : _begin{p}
    , _end{p+z}
    , _p{p}
    , _tok{Tokens::End}
    , _lineBegin{p}
  {}

  UChar nextChar() {
    return (_c = (_p < _end ? text::decodeUTF8Char(_p, _end) : UCharMax));
  }

  char nextByte() {
    return (_c = (_p < _end ? *_p++ : 0));
  }

  UChar peekNextChar() {
    auto p = _p;
    return p < _end ? text::decodeUTF8Char(p, _end) : UCharMax;
  }

  void undoChar() {
    _p -= text::UTF8SizeOf(_c);
  }

  void undoByte() {
    _p--;
  }

  // UChar peekNextNonSpaceChar() {
  //   auto p = _p;
  //   UChar c;
  //   while (p != _end) switch ((c = text::decodeUTF8Char(p, _end))) {
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
  //   while (p != _end) switch ((c = text::decodeUTF8Char(p, _end))) {
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

  void enqueueToken(Token t, const string& value) {
    updateSrcLocLength(t, _srcLoc);
    _tokQueue.enqueueLast(t, _srcLoc, value);
  }

  void enqueueTokenFirst(Token t, const string& value) {
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

  Token next() {
    if (_tok == '\n') {
      // Increase line if previous token was a linebreak. This way linebreaks are
      // semantically at the end of a line, rather than at the beginning of a line.
      incrLine();
    }

    // First, return the next queued token before reading more
    if (!_tokQueue.empty()) {
      _tokQueue.dequeueFirst(_tok, _srcLoc, _strval);
      return _tok;
    }

    beginTok();
    _strval.clear();
      // Set source location and clear _strval

    // The root switch has a dual purpose: Initiate tokens and reading symbols.
    // Because symbols are pretty much "anything else", this is the most
    // straight-forward way.
    bool isReadingIdent = false;
    #define ADDSYM_OR \
      if (isReadingIdent) { break; } else
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
          enqueueToken('\n', _strval);
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
          case ITextLit:  return readTextLit(/*isInterpolated=*/true);
          case '(':       return setTok(_c);
          default:        assert(!"invalid stack state"); break;
        }
      }

      case '{': case '}':
      case '[': case ']':
      case '<': case '>':
      case ',': case ';':
      case '+': case '*':
      case '!': case '@':
      case '=': case '#':
      // TODO: other operators, like &
        ENDSYM_OR return setTok(_c);

      case ':':  ENDSYM_OR return readColonOrAutoAssign();
      case '-':  ENDSYM_OR return readMinusOrRArr();
      case '/':  ENDSYM_OR return readSolidus();
      case '.':  ENDSYM_OR return readDot(); // "." | ".." | "..." | "."<float>
      case U'\u2702':   ENDSYM_OR return readDataTail(); // "✂" [^\n]* \n

      case '0':         ADDSYM_OR return readZeroLeadingNumLit();
      case '1' ... '9': ADDSYM_OR return readDecIntLit();

      case '\'':  return readCharLit();
      case '"':   return readTextLit(/*isInterpolated=*/false);
      case '`':   return readRawStringLit();

      default: {
        if (text::isValidChar(_c)) {
          isReadingIdent = true;
        } else {
          return error("Illegal character "+text::repr(_c)+" in input");
        }
        break;
      }
    }

    return setTok(isReadingIdent ? Identifier : End);
  }


  Token readColonOrAutoAssign() {
    // ColonOrAutoAssign = ":" | AutoAssign
    // AutoAssign        = ":="
    switch (nextChar()) {
      case UCharMax:        return error("Unexpected end of input");
      case '=':             return setTok(AutoAssign);
      default:  undoChar(); return setTok(U':');
    }
  }


  Token readMinusOrRArr() {
    // MinusOrRArr = "-" | RArr
    // RArr        = "->"
    switch (nextChar()) {
      case UCharMax:        return error("Unexpected end of input");
      case '>':             return setTok(RArr);
      default:  undoChar(); return setTok(U'-');
    }
  }


  // Token readEq() {
  //   // Eq   = "=" | EqEq
  //   // EqEq = "=="
  //   switch (nextChar()) {
  //     case UCharMax:        return error("Unexpected end of input");
  //     case '=':             return setTok(EqEq);
  //     default:  undoChar(); return setTok(U'=');
  //   }
  // }


  Token readSolidus() {
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
          readLineComment();
        } else {
          bool hasNewline;
          if (readGeneralComment(hasNewline) == Error) {
            return _tok;
          }
          if (insertSemic) {
            // if we should insert a semicolon before the comment,
            // only do so if the general comment contains a newline.
            insertSemic = hasNewline;
          }
        }

        if (insertSemic) {
          enqueueToken(_tok, _strval);
          return setTok(';');
        }

        return _tok;
      }

      default:  return setTok('/');
    }
  }


  Token readGeneralComment(bool& hasNewline) {
    // Enter at "/*", leave at "*/"
    hasNewline = false;
    FOREACH_CHAR {
      case '\n': {
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
        break;
      }
    }
    return error("unterminated general comment");
  }


  Token readLineComment() {
    // Enter at "//"
    FOREACH_CHAR {
      case '\n': undoChar(); return setTok(LineComment);
      default:   break;
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


  template <char TermC>
  bool readCharLitEscape() {
    // EscapedUnicodeChar  = "\a" | "\b" | "\f" | "\n" | "\r" | "\t"
    //                     | "\v" | "\\" | <TermC>
    //                     | LittleXUnicodeValue
    //                     | LittleUUnicodeValue
    //                     | BigUUnicodeValue
    // LittleXUnicodeValue = "\x" HexDigit HexDigit
    // LittleUUnicodeValue = "\u" HexDigit HexDigit HexDigit HexDigit
    // BigUUnicodeValue    = "\U" HexDigit HexDigit HexDigit HexDigit
    //                            HexDigit HexDigit HexDigit HexDigit
    switch (nextByte()) {
      case 'a': { _strval += '\x07'; break; } // alert or bell
      case 'b': { _strval += '\x08'; break; } // backspace
      case 'f': { _strval += '\x0C'; break; } // form feed
      case 'n': { _strval += '\x0A'; break; } // line feed or newline
      case 'r': { _strval += '\x0D'; break; } // carriage return
      case 't': { _strval += '\x09'; break; } // horizontal tab
      case 'v': { _strval += '\x0b'; break; } // vertical tab
      case TermC:
      case '\\': { _strval += (char)_c; break; }
      case 'x': { return readHexUChar(2); }
      case 'u': { return readHexUChar(4); }
      case 'U': { return readHexUChar(8); }
      default: {
        error(
          "Unexpected character escape sequence '\\" + text::repr(_c) + "'"
        );
        return false;
      }
    }
    return true;
  }


  void assignStrValTrimmed() {
    // Copies bytes from _begin[_srcLoc.offset] until _p to _strval
    const char* p = &_begin[_srcLoc.offset];
    size_t len = (_p - _begin) - _srcLoc.offset;
    assert(len > 1);
    p++;
    len -= 2;
    _strval.assign(p, len);
  }


  Token readCharLit() {
    // CharLit = "'" ( UnicodeChar | EscapedUnicodeChar<'> ) "'"
    switch (nextChar()) {
      case UCharMax:
        return error("Unterminated character literal at end of input");
      LINEBREAK_CASES
        return error("Illegal character in character literal");
      case '\'':
        return error("Empty character literal or unescaped ' in character literal");
      case '\\':
        if (!readCharLitEscape<'\''>()) return _tok; break;
      default: {
        assignStrValTrimmed(); // '...' => ...
        break;
      }
    }
    switch (nextChar()) {
      case '\'':      return setTok(CharLit);
      default:        return error("Expected ' to end single-character literal");
    }
  }


  Token readRawStringLitBuf() {
    // Called from readRawStringLit when we encounter a \r
    assignStrValTrimmed(); // `...\r => ...
    while (_p != _end) switch (nextByte()) {
      case '`':  return setTok(RawStringLit);
      case '\r': break; // ignore
      default: _strval += (char)_c; break; // store
    }
    return error("Unterminated raw string literal");
  }


  Token readRawStringLit() {
    // RawStringLit = "`" { UnicodeChar | NewLine } "`"
    while (_p != _end) switch (nextByte()) {
      case '`': return setTok(RawStringLit);
      case '\r': {
        // \r is ignore because Windows, so we need to copy  all bytes read
        // so far into _strval and then ignore this byte.
        return readRawStringLitBuf();
      }
      default: {
        if (!_strval.empty()) {
          _strval += (char)_c;
        }
        break;
      }
    }
    return error("Unterminated raw string literal");
  }


  Token readTextLit(bool isInterpolated) {
    // TextLit = '"' ( UnicodeChar | EscapedUnicodeChar<"> )* '"'
    FOREACH_CHAR {
      LINEBREAK_CASES return error("Illegal character in string literal");
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
        if (!readCharLitEscape<'"'>()) {
          return _tok;
        }
        break;
      }
      default: {

        #ifdef VALIDATE_UNICODE
        switch (text::category(_c)) {
          case text::Category::Unassigned:
          case text::Category::NormativeCs: {
            error(string{"Invalid Unicode character "} + text::repr(_c));
            return false;
          }
          default: break;
        }
        #endif // VALIDATE_UNICODE

        text::appendUTF8(_strval, _c);
        break;
      }
    }
    return error("Unterminated string literal");
  }


  bool readHexUChar(int nbytes) {
    const char* beginp = _p + 1;
    int i = 0;
    while (i++ != nbytes) {
      switch (nextByte()) {
        case '0' ... '9': case 'A' ... 'F': case 'a' ... 'f': {
          break;
        }
        default: {
          error("Invalid Unicode sequence");
          return false;
        }
      }
    }

    UChar c;
    if (!strtou32(beginp, nbytes, 16, c)) {
      assert(!"strtou32");
    }

    #ifdef VALIDATE_UNICODE
    switch (text::category(c)) {
      case text::Category::Unassigned:
      case text::Category::NormativeCs: {
        error(
          string{"Invalid Unicode character \\"} +
          (nbytes == 4 ? "u" : "U") + string{beginp, (size_t)nbytes}
        );
        return false;
      }
      default: break;
    }
    #endif // VALIDATE_UNICODE

    text::appendUTF8(_strval, c);
    return true;
  }


  // === Integer literals ===
  // int_lit     = decimal_lit | octal_lit | hex_lit
  // decimal_lit = ("0" | ( "1" … "9" ) { decimal_digit })
  // octal_lit   = "0" { octal_digit }
  // hex_lit     = "0" ( "x" | "X" ) hex_digit { hex_digit }
  //

  Token readZeroLeadingNumLit() {
    // enter at _c='0'
    FOREACH_BYTE {
      case 'X': case 'x': return readHexIntLit();
      case '.':           return readFloatLitAtDot();
      OCTNUM_CASES        return readOctIntLit();
      default: {
        undoByte();
        return setTok(DecIntLit); // special case '0'
      }
    }
    return setTok(DecIntLit); // special case '0' as last char in input
  }


  Token readOctIntLit() {
    // Enter at ('1' ... '7')
    FOREACH_BYTE {
      OCTNUM_CASES { break; }
      case '.': {
        // value.insert(0, 1, '0');
        return readFloatLitAtDot();
      }
      default: {
        undoByte();
        return setTok(OctIntLit);
      }
    }
    return setTok(OctIntLit);
  }


  Token readDecIntLit() {
    // Enter at ('1' ... '9')
    FOREACH_BYTE {
      DECNUM_CASES { break; }
      case 'e': case 'E':    return readFloatLitAtExp();
      case '.':              return readFloatLitAtDot();
      default: { undoByte(); return setTok(DecIntLit); }
    }
    return setTok(DecIntLit);
  }


  Token readHexIntLit() {
    // value.clear(); // ditch '0'. Enter at "x" in "0xN..."
    FOREACH_BYTE {
      HEXNUM_CASES { break; }
      default: { undoByte(); return setTok(HexIntLit); }
    }
    return error("Incomplete hex literal"); // special case: '0x' is last of input
  }


  Token readDot() {
    // enter at "."
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
      DECNUM_CASES         return readFloatLitAtDot();
      default: undoChar(); return setTok(U'.');
    }
  }


  Token readFloatLitAtDot() {
    // else enter at "<decnum>."
    //
    // float_lit = decimals "." [ decimals ] [ exponent ] |
    //             decimals exponent |
    //             "." decimals [ exponent ]
    // decimals  = decimal_digit { decimal_digit }
    // exponent  = ( "e" | "E" ) [ "+" | "-" ] decimals
    FOREACH_BYTE {
      DECNUM_CASES { break; }
      case 'e': case 'E':    return readFloatLitAtExp();
      default: { undoByte(); return setTok(FloatLit); }
    }
    return setTok(FloatLit); // special case '0.'
  }


  Token readFloatLitAtExp() {
    // enter at "<decnum>[E|e]", expect: [ "+" | "-" ] decimals
    if (_p == _end) return error("Incomplete float exponent");
    switch (nextByte()) {
      DECNUM_CASES case '+': case '-': break;
      default: goto illegal_exp_value;
    }

    if (_c == '+' || _c == '-') {
      if (_p == _end) return error("Incomplete float exponent");
      switch (nextByte()) {
        DECNUM_CASES { break; }
        default: { goto illegal_exp_value; }
      }
    }

    FOREACH_BYTE {
      DECNUM_CASES break;
      default: {
        undoByte();
        return setTok(FloatLit);
      }
    }

    return setTok(FloatLit); // special case '<decnum>[E|e]?[+|-]?<decnum>'

   illegal_exp_value:
    error("Illegal value '" + text::repr(_c) + "' for exponent in float literal");
    undoByte();
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

Token Lex::next() {
  return self->next();
}
// bool Lex::nextIfEq(Token pred) {
//   return self->nextIfEq(pred);
// }
Token Lex::current() const {
  return self->_tok;
}
void Lex::undoCurrent() {
  self->enqueueTokenFirst(self->_tok, self->_strval);
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

const std::string& Lex::interpretedTokValue() const {
  return self->_strval;
}

int Lex::tokValueCmp(const char* p, size_t len) const {
  if (self->_srcLoc.length == len) {
    return memcmp(p, self->_begin + self->_srcLoc.offset, len);
  }
  return len < self->_srcLoc.length ? -1 : 1;
}

void Lex::copyTokValue(string& s) const {
  s.assign(self->_begin + self->_srcLoc.offset, self->_srcLoc.length);
}

string Lex::byteStringTokValue() const {
  return {self->_begin + self->_srcLoc.offset, self->_srcLoc.length};
}

string Lex::repr(Token t, const string& value) {
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
