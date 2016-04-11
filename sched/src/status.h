// Copyright (c) 2012-2014 Rasmus Andersson <http://rsms.me/>
#pragma once
#include <ostream>

// Status/error type which has a very low cost when there's no error.
//
// - When representing "no error" (Status::OK()) the code generated is
//   simply a pointer-sized int.
//
// - When representing an error, a single memory allocation is incured at
//   construction which itself represents both the error code (first byte)
//   as well as any message (remaining bytes) terminated by a NUL sentinel
//   byte.
//

struct Status {
  using Code = uint8_t;

  static Status OK(); // == OK (no error)
  Status(Code);
  Status(Code, const std::string& error_message);
  Status(const std::string& error_message);

  Status() noexcept; // == OK (no error)
  Status(std::nullptr_t) noexcept; // == OK (no error)
  ~Status();

  bool ok() const;
  Code code() const;
  const char* message() const;

  Status(const Status& other);
  Status(Status&& other);
  Status& operator=(const Status& s);
  Status& operator=(Status&& s);

  bool operator==(const Status& s);
  bool operator==(const Code s);


// ---------------------------------------------------------------------------
private:
  void _set_state(Code code, const std::string& message);
  static const char* _copy_state(const char* other);
  // OK status has a NULL _state. Otherwise, _state is:
  //   _state[0]    == code
  //   _state[1..]  == message c-string
  const char* _state;
};

inline Status::Status() noexcept : _state{NULL} {} // == OK
inline Status::Status(std::nullptr_t) noexcept : _state{NULL} {} // == OK
inline Status::~Status() { if (_state != NULL) delete[] _state; }
inline Status Status::OK() { return Status(); }
inline bool Status::ok() const { return (_state == NULL); }
inline Status::Code Status::code() const {
  return (_state == NULL) ? 0 : _state[0];
}
inline const char* Status::message() const {
  return (_state == NULL) ? "" : &_state[1];
}

inline std::ostream& operator<< (std::ostream& os, const Status& st) {
  if (st.ok()) {
    return os << "OK";
  } else {
    return os << st.message() << " (#" << (int)st.code() << ')';
  }
}

inline const char* Status::_copy_state(const char* other) {
  if (other == 0) {
    return 0;
  } else {
    size_t size = strlen(&other[1]) + 2;
    char* state = new char[size];
    memcpy(state, other, size);
    return state;
  }
}

inline void Status::_set_state(Code code, const std::string& message) {
  char* state = new char[1 + message.size() + 1];
  state[0] = static_cast<char>(code);
  memcpy(state + 1, message.data(), message.size());
  state[1 + message.size()] = '\0';
  _state = state;
}

inline Status::Status(const Status& other) {
  _state = _copy_state(other._state);
}

inline Status::Status(Status&& other) {
  _state = 0;
  std::swap(other._state, _state);
}

inline Status::Status(Code code) {
  char* state = new char[2];
  state[0] = static_cast<char>(code);
  _state = state;
}

inline Status::Status(Code code, const std::string& message) {
  _set_state(code, message);
}

inline Status::Status(const std::string& message) {
  _set_state(0, message);
}

inline Status& Status::operator=(const Status& other) {
  if (_state != other._state) {
    delete[] _state;
    _state = _copy_state(other._state);
  }
  return *this;
}

inline Status& Status::operator=(Status&& other) {
  if (_state != other._state) {
    std::swap(other._state, _state);
  }
  return *this;
}

inline bool Status::operator==(const Status& s) {
  return _state == s._state ||
         (_state != nullptr &&
          s._state != nullptr &&
          strcmp(_state, s._state) == 0);
}

inline bool Status::operator==(const Code c) {
  return code() == c;
}
