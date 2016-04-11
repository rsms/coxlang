// Defer execution of a block of code until exiting the current scope.
//
//  struct Foo {
//    void hello(int x) { printf("Foo::hello(%d)\n", x); }
//  };
//  {
//    printf("entering block 0\n");
//    defer []{ printf("leaving block 0\n"); };
//    {
//      printf("  entering block 1\n");
//      Foo foo;
//      defer std::bind(&Foo::hello, &foo, 3);
//    }
//  }
//
// Output:
//
//  entering block 0
//    entering block 1
//    Foo::hello(3)
//  leaving block 0
//
#pragma once

namespace defer_detail {

template <typename F>
struct deferred {
  deferred(F f) : _f(f) {}
  deferred(const deferred<F>&) = delete;
  deferred(deferred<F>&& other) = default;
  ~deferred() { _f(); }
  F _f;
};

struct _defer {
  // Helper struct that enables the syntax `defer ...`
  template <typename F> inline deferred<F> operator=(F f) {
    return deferred<F>(f); }
};

} // namespace defer_detail

#define _defer_JOIN1(x, y) x ## y
#define _defer_JOIN(x, y) _defer_JOIN1(x, y)
#ifdef __clang__
  #define defer auto _defer_JOIN(_deferred_, __COUNTER__) = defer_detail::_defer() =
#else
  #define defer auto _defer_JOIN(_deferred_, __LINE__) = defer_detail::_defer() =
#endif
