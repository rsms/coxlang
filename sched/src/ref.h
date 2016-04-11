#pragma once
#include "atomic.h"

using RefCount = uint32_t;

struct AtomicRefCounter {
  static void retain(volatile RefCount& v) { __sync_add_and_fetch(&v, 1); }
  static bool release(volatile RefCount& v) {
    return __sync_sub_and_fetch(&v, 1) == 0;
  }
};

struct __attribute__((packed)) RefCounted {
  volatile RefCount __refc = 0;
};


enum RefTransfer_ { RefTransfer };


#define NULLABLE_REF_IMPL(T, self, Imp)                                 \
public:                                                                 \
  T(std::nullptr_t) : self{nullptr} {}                                  \
  T& operator=(std::nullptr_t) { return resetSelf(nullptr); }           \
  REF_IMPL(T, self, Imp)


#define REF_IMPL(T, self, Imp) \
  public: \
  struct Imp; friend Imp; \
  Imp* self = nullptr; \
  static void __dealloc(Imp*); \
  static void __retain(Imp* p) { \
    if (p) { AtomicRefCounter::retain(((RefCounted*)p)->__refc); } \
  } \
  static bool __release(Imp* p) { \
    return ( \
      p && \
      AtomicRefCounter::release(((RefCounted*)p)->__refc) && \
      ({ T::__dealloc(p); true; }) ); \
  } \
  REF_IMPL_COMMON(T, self, Imp)

// #define REF_IMPL_VTABLE(T, self, Imp) TODO

#define REF_IMPL_COMMON(T, self, Imp) \
  explicit T(Imp* p) : self{p} { __retain(self); } \
  explicit T(Imp* p, RefTransfer_) : self{p} {} \
  T(T const& rhs)    : self{rhs.self} { __retain(self); } \
  T(const T* rhs)    : self{rhs->self} { __retain(self); } \
  T(T&& rhs)         : self{std::move(rhs.self)} { rhs.self = nullptr; } \
  ~T() { __release(self); } \
  T& resetSelf(const Imp* p = nullptr) const { \
    Imp* old = self; \
    __retain((const_cast<T*>(this)->self = const_cast<Imp*>(p))); \
    __release(old); \
    return *const_cast<T*>(this); \
  } \
  Imp* stealSelf() { Imp* p = self; self = nullptr; return p; } \
  T& operator=(T&& rhs) { \
    if (self != rhs.self) { \
      __release(self); \
      self = rhs.self; \
    } \
    rhs.self = nullptr; \
    return *this; \
  } \
  T& operator=(const T& rhs) { return resetSelf(rhs.self); } \
  T& operator=(Imp* rhs) { return resetSelf(rhs); } \
  T& operator=(const Imp* rhs) { return resetSelf(rhs); } \
  bool operator==(const T& other) const { return self == other.self; } \
  bool operator!=(const T& other) const { return self != other.self; } \
  bool operator==(std::nullptr_t) const { return self == nullptr; } \
  bool operator!=(std::nullptr_t) const { return self != nullptr; } \
  operator bool() const { return self != nullptr; } \
  Imp* operator->() const { return self; }

