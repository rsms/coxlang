#pragma once
#include <stdint.h>
#include <string.h>
#include <string>
#include <ostream>
#include <strstream>

// Error type which has a very low cost when there's no error.
//
// - When representing "no error" (Err::OK()) the code generated is simply
//   a pointer-sized int.
//
// - When representing an error, a single memory allocation is incured at
//   construction which itself represents both the error code (first byte)
//   as well as any message (remaining bytes) terminated by a NUL sentinel byte.
//

struct Err {
  using Code = uint32_t;

  static Err OK(); // == OK (no error)
  Err(Code);
  Err(Code, const std::string& msg);
  Err(const std::string& msg);

  template <size_t N>
  Err(Code, const char(&msg)[N]);
  
  template <size_t N>
  Err(const char(&msg)[N]);

  template <typename Arg, typename... Args>
  Err(Code, Arg, Args...);

  Err() noexcept; // == OK (no error)
  Err(std::nullptr_t) noexcept; // == OK (no error)
  ~Err();

  bool ok() const; // true if no error
  Code code() const;
  const char* message() const;
  operator bool() const;    // == !ok() -- true when representing an error

  Err(const Err& other);
  Err(Err&& other);
  Err& operator=(const Err& s);
  Err& operator=(Err&& s);


// ------------------------------------------------------------------------------------------------
private:
  void _set_state(Code code, const std::string& message);
  static const char* _copy_state(const char* other);
  // OK status has a NULL _state. Otherwise, _state is:
  //   _state[0..sizeof(Code)]  == code
  //   _state[sizeof(Code)..]   == message c-string
  const char* _state;
};

inline Err::Err() noexcept : _state{NULL} {} // == OK
inline Err::Err(std::nullptr_t) noexcept : _state{NULL} {} // == OK

inline Err::~Err() {
  if (_state) {
    delete[] _state;
  }
}

inline Err Err::OK() { return Err{}; }
inline bool Err::ok() const { return !_state; }
inline Err::operator bool() const { return _state; }
inline Err::Code Err::code() const {
  return (_state == nullptr) ? 0 : *((Code*)_state);
}
inline const char* Err::message() const { return _state ? &_state[sizeof(Code)] : ""; }

inline std::ostream& operator<< (std::ostream& os, const Err& e) {
  if (!e) {
    return os << "OK";
  } else {
    return os << e.message() << " (#" << e.code() << ')';
  }
}

inline const char* Err::_copy_state(const char* other) {
  if (other == 0) {
    return 0;
  } else {
    size_t size = strlen(&other[sizeof(Code)]) + sizeof(Code) + 1;
    char* state = new char[size];
    memcpy(state, other, size);
    return state;
  }
}

inline void Err::_set_state(Code code, const std::string& msg) {
  char* state = new char[sizeof(Code) + msg.size() + 1];
  *(Code*)state = code;
  memcpy(state + sizeof(Code), msg.data(), msg.size());
  state[sizeof(Code) + msg.size()] = '\0';
  _state = state;
}

template <size_t N>
inline Err::Err(Code code, const char(&msg)[N]) {
  _state = new char[sizeof(Code) + N]; // note: N contains space for nul byte
  *((Code*)_state) = code;
  memcpy((void*)(_state + sizeof(Code)), msg, N); // msg[N-1]=='\0'
}

template <size_t N>
inline Err::Err(const char(&msg)[N]) : Err(0, msg) {}

template <typename Arg, typename... Args>
inline Err::Err(Code code, Arg arg0, Args... args) {
  std::strstream s;
  s << std::forward<Arg>(arg0);
  using expander = int[];
  (void)expander{0, (void(s << std::forward<Args>(args)),0)...};
  size_t z = sizeof(Code) + s.pcount() + 1;
  _state = new char[z];
  *((Code*)_state) = code;
  memcpy((void*)(_state + sizeof(Code)), s.str(), s.pcount());
  ((char*)_state)[z-1] = '\0';
}

inline Err::Err(const Err& other) {
  _state = _copy_state(other._state);
}

inline Err::Err(Err&& other) {
  _state = nullptr;
  std::swap(other._state, _state);
}

inline Err::Err(Code code) {
  char* state = new char[sizeof(Code)+1];
  *(Code*)state = code;
  state[sizeof(Code)] = '\0';
  _state = state;
}

// inline Err::Err(int code) : Err{(Err::Code)code} {}
// inline Err::Err(int code, const std::string& msg) : Err{(Err::Code)code, msg} {}

inline Err::Err(Code code, const std::string& msg) {
  _set_state(code, msg);
}

inline Err::Err(const std::string& msg) {
  _set_state(0, msg);
}

inline Err& Err::operator=(const Err& other) {
  if (_state != other._state) {
    delete[] _state;
    _state = _copy_state(other._state);
  }
  return *this;
}

inline Err& Err::operator=(Err&& other) {
  if (_state != other._state) {
    std::swap(other._state, _state);
  }
  return *this;
}
