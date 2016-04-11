#pragma once
#include <stdint.h>
#include <cstddef>
#include <utility>
namespace rx {

using refcount_t = uint32_t;

struct PlainRefCounter {
  using value_type = refcount_t;
  static void retain(value_type& v) { ++v; }
  static bool release(value_type& v) { return --v == 0; }
};

struct AtomicRefCounter {
  using value_type = volatile refcount_t;
  static void retain(value_type& v) { __sync_add_and_fetch(&v, 1); }
  static bool release(value_type& v) { return __sync_sub_and_fetch(&v, 1) == 0; }
};


template <typename T>
struct Ref {
  T* self;
  Ref() : self{nullptr} {}
  Ref(std::nullptr_t) : self{nullptr} {}
  explicit Ref(T* p, bool add_ref=false) : self{p} { if (add_ref && p) { self->retainRef(); } }
  Ref(const Ref& rhs) : self{rhs.self} { if (self) self->retainRef(); }
  Ref(const Ref* rhs) : self{rhs->self} { if (self) self->retainRef(); }
  Ref(Ref&& rhs) { self = std::move(rhs.self); rhs.self = 0; }
  ~Ref() { if (self) self->releaseRef(); }
  void resetSelf(std::nullptr_t=nullptr) const {
    if (self) {
      auto* s = const_cast<Ref*>(this);
      s->self->releaseRef();
      s->self = nullptr;
    }
  }
  Ref& resetSelf(const T* p) const {
    T* old = self;
    const_cast<Ref*>(this)->self = const_cast<T*>(p);
    if (self) self->retainRef();
    if (old) old->releaseRef();
    return *const_cast<Ref*>(this);
  }
  Ref& operator=(const Ref& rhs) {  return resetSelf(rhs.self); }
  Ref& operator=(T* rhs) {          return resetSelf(rhs); }
  Ref& operator=(const T* rhs) {    return resetSelf(rhs); }
  Ref& operator=(std::nullptr_t) {  return resetSelf(nullptr); }
  Ref& operator=(Ref&& rhs) {
    if (self != rhs.self && self) {
      self->releaseRef();
      self = nullptr;
    }
    std::swap(self, rhs.self);
    return *this;
  }
  T* operator->() const { return self; }
  operator bool() const { return self != nullptr; }
};


template <typename T, class C>
struct RefCounted {
  typename C::value_type __refc = 1;
  virtual ~RefCounted() = default;
  void retainRef() { C::retain(__refc); }
  bool releaseRef() { return C::release(__refc) && ({ delete this; true; }); }
  using Ref = Ref<T>;
};

template <typename T> using UnsafeRefCounted = RefCounted<T, PlainRefCounter>;
template <typename T> using SafeRefCounted = RefCounted<T, AtomicRefCounter>;

// Example:
//
//   struct ReqBase {
//     uv_fs_t uvreq;
//   };
//  
//   struct StatReq final : ReqBase, SafeRefCounted<StatReq> {
//     StatReq(StatCallback&& cb) : Req{}, cb{fwdarg(cb)} {}
//     StatCallback cb;
//   };
//

// ===============================================================================================
// Mix-in version

#define RX_REF_COUNT_CONSTANT rx::refcount_t(0xffffffffu)

struct __attribute__((packed)) ref_counted_novtable {
  // When you don't need a vtable
  typename rx::AtomicRefCounter::value_type __refc = 1;
};

enum RefTransfer_ { RefTransfer };

#define RX_REF_MIXIN_NOVTABLE(T) \
  public: \
    struct Imp; friend Imp; \
    static void __dealloc(Imp*); \
    static void __retain(Imp* p) { \
      if (p && ((::rx::ref_counted_novtable*)p)->__refc != RX_REF_COUNT_CONSTANT) \
        rx::AtomicRefCounter::retain(((::rx::ref_counted_novtable*)p)->__refc); \
    } \
    static bool __release(Imp* p) { \
      return ( \
        p && \
        ((::rx::ref_counted_novtable*)p)->__refc != RX_REF_COUNT_CONSTANT && \
        rx::AtomicRefCounter::release(((::rx::ref_counted_novtable*)p)->__refc) && \
        ({ T::__dealloc(p); true; }) ); \
    } \
    RX_REF_MIXIN_BODY(T, Imp)


#define RX_REF_MIXIN_BODY(T,Imp) \
  Imp* self = nullptr; \
  \
  T(std::nullptr_t)  : self{nullptr} {} \
  explicit T(Imp* p) : self{p} { __retain(self); } \
  explicit T(Imp* p, rx::RefTransfer_) : self{p} {} \
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
  T& operator=(std::nullptr_t) { return resetSelf(nullptr); } \
  bool operator==(const T& other) const { return self == other.self; } \
  bool operator!=(const T& other) const { return self != other.self; } \
  bool operator==(std::nullptr_t) const { return self == nullptr; } \
  bool operator!=(std::nullptr_t) const { return self != nullptr; } \
  operator bool() const { return self != nullptr; } \
  Imp* operator->() const { return self; }


} // namespace
