#pragma once
#include "atomic.h"

// struct RefCounted {
//   virtual void retainRef() const = 0;
//   virtual bool releaseRef() const = 0;
//   virtual bool hasOneRef() const = 0;
// };


#define REF_IMP_FIELDS \
  atomic_t(uint32_t) _refcount = 0;

struct RefCounted {
  atomic_t(uint32_t) _refcount = 0;
};


#define NULLABLE_REF_IMPL(T, self, Imp)                                 \
public:                                                                 \
  T(std::nullptr_t) : self{nullptr} {}                                  \
  T& operator=(std::nullptr_t) { return setself(nullptr); }             \
  REF_IMPL(T, self, Imp)


#define REF_IMPL(T, self, Imp)                                          \
public:                                                                 \
  explicit T(Imp* p) : self{p} { if (self) { retainRef(); } }           \
  T(T const& b) : self{b.self} { if (self) { retainRef(); } }           \
  T(const T* b) : self{b->self} { if (self) { retainRef(); } }          \
  T(T&& b) : self{std::move(b.self)} { b.self = nullptr; }              \
  ~T() { if (self) { _releaseRef(self); } }                             \
                                                                        \
  T& operator=(T&& b) {                                                 \
    if (self != b.self) {                                               \
      _releaseRef(self);                                                \
      self = b.self;                                                    \
    }                                                                   \
    b.self = nullptr;                                                   \
    return *this;                                                       \
  }                                                                     \
  T& operator=(const T& b) { return setself(b.self); }                  \
  T& operator=(Imp* p) { return setself(p); }                           \
  T& operator=(const Imp* p) { return setself(p); }                     \
  bool operator==(const T& b) const { return self == b.self; }          \
  bool operator!=(const T& b) const { return self != b.self; }          \
  bool operator==(std::nullptr_t) const { return self == nullptr; }     \
  bool operator!=(std::nullptr_t) const { return self != nullptr; }     \
  operator bool() const { return self != nullptr; }                     \
  Imp* operator->() const { return self; }                              \
                                                                        \
  T& setself(const Imp* p) const {                                      \
    Imp* old = self;                                                    \
    Imp* p2 = (const_cast<T*>(this)->self = const_cast<Imp*>(p));       \
    if (p2) { _retainRef(p2); }                                         \
    if (old) { _releaseRef(old); }                                      \
    return *const_cast<T*>(this);                                       \
  }                                                                     \
  void retainRef() const {                                              \
    _retainRef(self);                                                   \
  }                                                                     \
  bool releaseRef() const {                                             \
    if (_releaseRef(self)) {                                            \
      const_cast<T*>(this)->self = nullptr;                             \
      return true;                                                      \
    }                                                                   \
    return false;                                                       \
  }                                                                     \
  bool hasOneRef() const {                                              \
    return AtomicLoad(&self->_refcount) == 1;                           \
  }                                                                     \
private:                                                                \
  void _retainRef(Imp* p) const {                                       \
    AtomicFetchAdd(&p->_refcount, 1, AtomicRelaxed);                    \
  }                                                                     \
  bool _releaseRef(Imp* p) const {                                      \
    if (AtomicFetchSub(&p->_refcount, 1, AtomicRelaxed) == 1) {         \
      delete p;                                                         \
      return true;                                                      \
    }                                                                   \
    return false;                                                       \
  }                                                                     \
  Imp* volatile self = nullptr;



#define REF_COUNT_IMPL(T)                                      \
  public:                                                      \
    void retainRef() const override {                          \
      AtomicFetchAdd(&_refcount, 1, AtomicRelaxed);            \
    }                                                          \
    bool releaseRef() const override {                         \
      if (AtomicFetchSub(&_refcount, 1, AtomicRelaxed) == 1) { \
        delete static_cast<const T*>(this);                    \
        return true;                                           \
      }                                                        \
      return false;                                            \
    }                                                          \
    bool hasOneRef() const override {                          \
      return AtomicLoad(&_refcount) == 1;                      \
    }                                                          \
  private:                                                     \
    atomic_t(uint32_t) _refcount = 0;
