#pragma once
#include <string>
#include <ostream>

using UChar                 = char32_t;
using Text                  = std::basic_string<UChar>; // Unicode text
static const UChar UCharMax = UINT32_MAX;

namespace text {
using std::string;

Text decodeUTF8(const string&);
  // Convert a UTF8 string to Unicode text.

string encodeUTF8(const Text&);
string encodeUTF8(UChar);
  // Convert Unicode text into a UTF8 string.

size_t UTF8SizeOf(UChar);
  // Number of bytes needed to encode a character as UTF8

string repr(UChar);
string repr(const Text&);
  // Printable UTF8 representation with non-graphic characters encoded as U+X{4,8}

bool isValidChar(UChar);      // True if assigned by Unicode
bool isDecimalDigit(UChar);   // 0-9
bool isHexDigit(UChar);       // 0-9,A-F,a-f
bool isWhitespaceChar(UChar); // True if considered whitespace (Category::NormativeZs)
bool isControlChar(UChar);    // True if control (Category::NormativeCc)
bool isLinebreakChar(UChar);  // True if pure linebreak (LF, CR, LINE- and PARAGRAPH SEPARATOR)
bool isGraphicChar(UChar);    // True if the char can be printed to represent itself graphically.
UChar caseFold(UChar);        // Normalize case of character through Unicode folding (1:1/basic)

enum Category : uint8_t;
Category category(UChar);
  // Look up the Unicode category classification of a character.

enum Category : uint8_t {
  Unassigned = 0, // Not Assigned
  InformativeLm,  // Letter, Modifier
  InformativeLo,  // Letter, Other
  InformativePc,  // Punctuation, Connector
  InformativePd,  // Punctuation, Dash
  InformativePe,  // Punctuation, Close
  InformativePf,  // Punctuation, Final quote (may behave like Ps or Pe depending on usage)
  InformativePi,  // Punctuation, Initial quote (may behave like Ps or Pe depending on usage)
  InformativePo,  // Punctuation, Other
  InformativePs,  // Punctuation, Open
  InformativeSc,  // Symbol, Currency
  InformativeSk,  // Symbol, Modifier
  InformativeSm,  // Symbol, Math
  InformativeSo,  // Symbol, Other
  NormativeCc,    // Other, Control
  NormativeCf,    // Other, Format
  NormativeCo,    // Other, Private Use
  NormativeCs,    // Other, Surrogate
  NormativeLl,    // Letter, Lowercase
  NormativeLt,    // Letter, Titlecase
  NormativeLu,    // Letter, Uppercase
  NormativeMc,    // Mark, Spacing Combining
  NormativeMe,    // Mark, Enclosing
  NormativeMn,    // Mark, Non-Spacing
  NormativeNd,    // Number, Decimal Digit
  NormativeNl,    // Number, Letter
  NormativeNo,    // Number, Other
  NormativeZl,    // Separator, Line
  NormativeZp,    // Separator, Paragraph
  NormativeZs,    // Separator, Space
  Assigned,       // Special category returned by `category` when the character is not unassigned,
                  // but we don't have detailed category information. Used by `isValidChar`.
  // This enum must match that of text.def's RX_TEXT_CHAR_CAT_* constants.
  // See http://www.unicode.org/notes/tn36/ and http://www.unicode.org/notes/tn36/Categories.txt
};


// ===============================================================================================

inline size_t UTF8SizeOf(UChar c) {
  return (c < 0x80) ? 1 : (c < 0x800) ? 2 : (c < 0x10000) ? 3 : 4;
}

inline bool isValidChar(UChar c) {
  return category(c) != Category::Unassigned;
}

inline bool isDecimalDigit(UChar c) {
  return c > ('0'-1) && c < ('9'+1);
}

inline bool isHexDigit(UChar c) {
  return isDecimalDigit(c) ||
         (c > ('A'-1) && c < ('F'+1)) ||
         (c > ('a'-1) && c < ('f'+1)) ;
}

inline bool isWhitespaceChar(UChar c) {
  return category(c) == Category::NormativeZs;
}

inline bool isControlChar(UChar c) {
  return category(c) == Category::NormativeCc;
}

} // namespace text

inline std::ostream& operator<< (std::ostream& os, const Text& v) {
  return os << text::encodeUTF8(v);
}

namespace std {
  inline std::string to_string(const Text& text) { return text::encodeUTF8(text); }
}

