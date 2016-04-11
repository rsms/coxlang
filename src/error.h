#pragma once
#include <stdint.h>
#include <string.h>
#include <string>
#include <ostream>

// Error type which has a very low cost when there's no error.
//
// - When representing "no error" (Err::OK()) the code generated is simply a pointer-sized int.
//
// - When representing an error, a single memory allocation is incured at construction which itself
//   represents both the error code (first byte) as well as any message (remaining bytes) terminated
//   by a NUL sentinel byte.
//

struct Err {
  using Code = uint32_t;

  static Err OK(); // == OK (no error)
  Err(Code);
  Err(Code, const std::string& error_message);
  Err(const std::string& error_message);

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
inline Err::~Err() { if (_state) delete[] _state; }
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

inline void Err::_set_state(Code code, const std::string& message) {
  char* state = new char[sizeof(Code) + message.size() + 1];
  *(Code*)state = code;
  memcpy(state + sizeof(Code), message.data(), message.size());
  state[sizeof(Code) + message.size()] = '\0';
  _state = state;
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

inline Err::Err(const std::string& message) {
  _set_state(0, message);
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
