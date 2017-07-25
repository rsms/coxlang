#pragma once
#include "error.h"
#include "text.h"
#include "srcloc.h"

#define RX_LEX_TOKENS(T) \
  /* Name, (0=no_value, 1=has_value, '('=group_start, ')'=group_end) */ \
  T( Error,            0   ) \
  T( End,              0   ) \
  T( RArr,             0   )/*  ->   */ \
  T( AutoAssign,       0   )/*  :=   */ \
  T( DotDot,           0   )/*  ..   */ \
  T( DotDotDot,        0   )/*  ...  */ \
  T( Identifier,       1   )/*   */ \
  T( BeginLit, '('         )/*   */ \
    T( BeginNumLit, '('    )/*   */ \
      T( DecIntLit,    1   )/*   */ \
      T( OctIntLit,    1   )/*   */ \
      T( HexIntLit,    1   )/*   */ \
      T( FloatLit,     1   )/*   */ \
    T( EndNumLit, ')'      )/*   */ \
    T( CharLit,        1   )/*   */ \
    T( RawStringLit,   1   )/*   */ \
    T( TextLit,        1   )/*   */ \
    T( ITextLit,       1   )/*   */ \
    T( ITextLitEnd,    1   )/*   */ \
  T( EndLit, ')'           )/*   */ \
  T( BeginComment, '('     )/*   */ \
    T( LineComment,      1 )/*  //  */ \
    T( GeneralComment,   1 )/*  /<asterix>...<asterix>/  */ \
  T( EndComment, ')'       )/*   */ \
  T( DataTail,         0   )/*  __END__ ...  */ \

#define RX_LEX_TOKENS_DEFINED

struct Lex {
  using Token = UChar;
  Lex(const char* p, size_t z);
  ~Lex();

  // True while Lex is valid (i.e. valid to call next() and current())
  bool isValid() const;

  // Return or take the last error
  const Err& lastError() const;
  Err&& takeLastError();

  // Advance to next token or read the current token
  Token next();
  // bool nextIfEq(Token pred);
  Token current() const;

  // Queue current tok to be returned from next next() call.
  // Does not affect value of current(), only next().
  // Multiple subsequent calls have no effect.
  void undoCurrent();
  Token queuedToken(); // returns Error if none

  // Scans ahead without changing state, and
  // if the first non-whitespace-or-control is predicatech,
  // advance lexer to that position and return true.
  // Returns false if the first non-whitespace-or-control is not predicatech.
  // When this function returns true, the next call to Lex::next() will
  // read the token _after_ predicatech (_not including_ predicatech.)
  // This is used by the parser to look ahead for
  // e.g. "is the next character '.'? Then let's parse a QualifiedIden"
  // bool nextCharIf(UChar predicatech);

  // Scans ahead without changing state and
  // returns the first character that is not whitespace or control.
  // UChar peekNextChar();

  // Get the current token value.
  // These methods access the raw value, as in the exact bytes from the
  // source code that represents the current token.
  const char* byteTokValue(size_t&) const;
  std::string byteStringTokValue() const;
  void copyTokValue(std::string& s) const; // copies token value byte to s

  // value of interpreted literals. (string and char)
  // This method returns an array of bytes that represent an interpreted
  // value, potentially different from the source-code bytes.
  const std::string& interpretedTokValue() const;

  // Compare provided string to current raw token value. I.e. strcmp(str, tokval)
  int tokValueCmp(const char* str, size_t len) const;
  template<size_t N> int tokValueCmp(char const(&str)[N]) {
    return tokValueCmp(str, N-1);
  }

  // Source location of current token
  const SrcLoc& srcLoc() const;

  // Creates a string representation of a token suitable for display
  static std::string repr(Token, const std::string& value);

  // Lexer state snapshotting and restoration
  struct Snapshot;
  Snapshot createSnapshot();
  void restoreSnapshot(const Snapshot&);
  void swapSnapshot(Snapshot&);

  enum Tokens : Token {
    BeginSpecialTokens = 0xFFFFFF, // way past last valid Unicode point
    #define T(Name, HasValue) Name,
    RX_LEX_TOKENS(T)
    #undef T
  };

private:
  struct Imp; Imp* self = nullptr;
public:
  struct Snapshot {
    Snapshot() {}
    Snapshot(Imp*s) : self{s} {}
    Snapshot(const Snapshot&) = delete;
    Snapshot(Snapshot&&) = default;
    ~Snapshot();
    Imp* self = nullptr;
  };
};
