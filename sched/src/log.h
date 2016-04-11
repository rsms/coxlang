#pragma once

#ifdef __cplusplus

// Example:
//
//   rxlog("Response code: " << rsp.code() << " " << rsp.name());
//   rxlogerr("chmod(\"" << filename << "\", " << mode << ")");
//
#if defined(DEBUG)
  #define rxtrace()    rxlogl(::rx::log_level::Trace, __PRETTY_FUNCTION__)
  #define rxlog(...)   rxlogl(::rx::log_level::Debug, __VA_ARGS__)
#else
  #define rxtrace()     do{}while(0)
  #define rxlog(...)   do{}while(0)
#endif
#define rxlogwarn(...) rxlogl(::rx::log_level::Warning, __VA_ARGS__)
#define rxlogerr(...)  rxlogl(::rx::log_level::Error, __VA_ARGS__)

// ----------------------------------------------------------------------------
#define rxlogl(level, ...) do { \
  bool __is_tty = ::isatty(1); \
  std::ostringstream __logbuf = std::ostringstream(); \
  ::rx::log_begin(__logbuf, __is_tty, level) << __VA_ARGS__; \
  ::rx::log_end(__logbuf, __is_tty, level, __FILE__, __LINE__); \
} while(0)

#include <unistd.h> // for isatty
#include <iostream>
#include <sstream>

namespace rx {

enum class log_level { Trace, Debug, Warning, Error };

inline std::ostream& log_begin(std::ostringstream& s, bool is_tty, const log_level level) {
  const char* suffix;
  if (is_tty) {
    switch (level) {
      case log_level::Trace:   s << "\e[1;32m "; break;
      case log_level::Debug:   s << "\e[1;34m "; break;
      case log_level::Warning: s << "\e[1;33m "; break;
      case log_level::Error:   s << "\e[1;31m "; break;
    }
    suffix = " \e[0m ";
  } else {
    suffix = " ";
  }
  switch (level) {
    case log_level::Trace:   return s << 'T' << suffix;
    case log_level::Debug:   return s << 'D' << suffix;
    case log_level::Warning: return s << 'W' << suffix;
    case log_level::Error:   return s << 'E' << suffix;
  }
}

inline void log_end(std::ostringstream& s, bool is_tty, const log_level level,
                    const char *filename, const int lineno)
{
  if (level == log_level::Trace) {
    s << " at " << (is_tty ? "\e[1;32m" : "") << filename << (is_tty ? "\e[0m" : "") << ':' << lineno;
  } else {
    s << (is_tty ? " \e[1;30m[" : " [") << filename << ':' << lineno << (is_tty ? "]\e[0m" : "]");
  }
  // fprintf(stderr, "%s\n", s.str().c_str()); fflush(stderr);
  ::flockfile(stderr);
  for (char c : s.str()) {
    putc_unlocked(c, stderr);
  }
  putc_unlocked('\n', stderr);
  ::funlockfile(stderr);
}

} // namespace rx

#else // if __cplusplus

// TODO: C logging
#define rxtrace()      do{}while(0)
#define rxlog(...)     do{}while(0)
#define rxlogwarn(...) do{}while(0)
#define rxlogerr(...)  do{}while(0)

#endif // __cplusplus

