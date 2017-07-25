#include "istr.h"

const char* kIStrEmptyCStr = "";

IStr::Imp* IStr::Imp::create(const char* s, uint32_t length, uint32_t hash) {
  uint32_t cstr_size = length+1;
  Imp* self = (Imp*)malloc(sizeof(Imp) + cstr_size);
  if (self) {
    self->__refc = 1;
    self->_hash = hash;
    self->_size = length;
    memcpy((void*)&self->_cstr, (const void*)s, cstr_size);
    const_cast<char*>(self->_cstr)[length] = '\0';
    self->_p.ps = (cstr_size == 1) ? kIStrEmptyCStr : 0;
      // cstr_size==1 means that the string is empty. Since we rely on some really funky hacks
      // where we check _cstr[0] for NUL, and fall back on _p.ps, we need to set _p.ps to a
      // constant C-string (the empty string).
  }
  return self;
}


// ------------------------------------------------------------------------------------------------

#define ISTRSET_TMPWRAP \
  assert(s); \
  if (len == 0xffffffffu) len = strlen(s); \
  auto sw = ConstIStr(s, len); \
  IStr::Imp* obj = (IStr::Imp*)&sw;


IStr IStr::Set::get(const char* s, uint32_t len) {
  // Return or create a IStr object representing the byte array of `len` at `s`
  ISTRSET_TMPWRAP

  auto P = _set.emplace(obj);
  if (P.second) {
    // Did insert. Update inserted pointer with the address of a new IStr::Imp
    obj = IStr::Imp::create(s, len, sw._hash);
    IStr::Imp** vp = const_cast<IStr::Imp**>(&*P.first); // IStr::Imp*const*
    *vp = obj;
  } else {
    // Already exists
    obj = *P.first;
  }

  return IStr{obj};
}


IStr IStr::Set::find(const char* s, uint32_t len) {
  ISTRSET_TMPWRAP
  auto I = _set.find(obj);
  return I == _set.end() ? nullptr : IStr{*I};
}


IStr IStr::WeakSet::get(const char* s, uint32_t len) {
  // Return or create a IStr object representing the byte array of `len` at `s`
  ISTRSET_TMPWRAP
    // Here we wrap `s` so that we can look it up without copying the string. In the case that `s`
    // is already represented in the set, the whole operation is cheap in the sense that no copies
    // are created to deallocated.

  auto P = _set.emplace(obj);
  if (P.second || !P.first->self) {
    // Did insert. Update inserted pointer with the address of a new IStr::Imp
    // printf("intern MISS (%s) '%s'\n", (!P.first->self ? "reuse-slot" : "new-slot"), s);
    obj = IStr::Imp::create(s, len, sw._hash);
    WeakRef* ws = ((WeakRef*)(&*P.first));
    ws->self = obj;
    ws->_bind();
  } else {
    // Does exist
    // printf("intern HIT '%s'\n", s);
    obj = P.first->self;
    IStr::__retain(obj);
  }

  return IStr{obj, rx::RefTransfer};
}


IStr IStr::WeakSet::find(const char* s, uint32_t len) {
  ISTRSET_TMPWRAP
  auto I = _set.find(obj);
  return I == _set.end() ? nullptr : IStr{I->self};
}

#undef ISTRSET_TMPWRAP
