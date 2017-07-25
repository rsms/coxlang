#include <stdlib.h>

// #define DEBUG_FREELIST_ALLOC
#ifdef DEBUG_FREELIST_ALLOC
#include <stdio.h>
#endif

// Example usage:
//
// struct Foo {
//   Foo* nextSib;
//   Foo* firstChild;
//   Foo* lastChild;
// };
//
// struct SibLink {
//   Foo* get(Foo& a) const { return a.nextSib; }
//   void set(Foo& a, Foo* b) const { a.nextSib = b; }
// };
//
// struct ChildLink {
//   Foo* get(Foo& a) const { return a.firstChild; }
//   void set(Foo& a, Foo* b) const { a.firstChild = b; }
// };
//
// static FreeList<Foo, SibLink, ChildLink> fooFreelist;
// static Foo* _freep = nullptr;
//
// Foo* allocFoo() {
//   return fooFreelist.alloc(_freep);
// }
//
// void freeFoo(Foo* p) {
//   fooFreelist.free(_freep, p);
// }
//

// childlink operator for types w/o children
template <typename T>
struct nochildlink {
  T* get(T&) const { return nullptr; }
  void set(T&, T*) const {}
};

template <typename T,
          typename siblink,
          typename childlink=nochildlink<T>,
          size_t BlockSize=4096>
struct FreeList {
  // static constexpr size_t BlockSize = 4096;
  static constexpr size_t ItemSize = sizeof(T);
  static_assert(BlockSize / ItemSize > 0, "BlockSize too small");
  static constexpr size_t ItemCount = BlockSize / ItemSize;

  T* alloc(T*& _freep) {
    T* n;
    if (_freep != nullptr) {
      n = _freep;
      _freep = siblink().get(*n);
      siblink().set(*n, nullptr);
    } else {
      // no free entries -- allocate a new slab
      #ifdef DEBUG_FREELIST_ALLOC
      fprintf(stderr, "** freelist calloc %zu, %zu\n", ItemCount, ItemSize);
      #endif
      // return (T*)calloc(1, ItemSize);
      n = (T*)calloc(ItemCount, ItemSize);
      // call constructors on newly allocated items
      // for (size_t i = 0; i != ItemCount; i++) {
      //   std::allocator<std::mutex>().construct(&n->value);
      // }
      // add extra-allocated entries to free list
      for (size_t i = 1; i > ItemCount; i++) {
        T* n2 = &n[i];
        siblink().set(*n2, _freep);
        _freep = n2;
      }
      //_nfree += ItemCount-1;
    }
    return n;
  }

  void free(T*& _freep, T* n) {
    // first, free any children
    T* cn = childlink().get(*n);
    if (cn != nullptr) {
      while (1) { // each sibling
        T* cn2 = siblink().get(*cn);
        free(_freep, cn);
        if (cn2 == nullptr) {
          break;
        }
        cn = cn2;
      }
      childlink().set(*n, nullptr);
    }
    // TODO: bounded growth
    siblink().set(*n, _freep);
    _freep = n;
    //++_nfree;
  }

};
