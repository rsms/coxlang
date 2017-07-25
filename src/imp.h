#pragma once
#include "srcloc.h"
#include "ast.h"
#include <map>
#include <set>
#include <string>

struct ImportSpec {
  AstNode* name = nullptr;  // null for unnamed imports
  SrcLoc   loc;
};

// Less comparator for ImportSpec on `name`.
// An empty ImportSpec is considered "less" than any other ImportSpec.
struct ImportSpecLess {
  constexpr bool operator()(const ImportSpec &a, const ImportSpec &b) const {
    return (a.name == nullptr) ? (b.name != nullptr) :
           (b.name == nullptr) ? (a.name == nullptr) :
           IStr::Less()(a.name->value.str, b.name->value.str) ;
  }
};

// Imports is an ordered mapping of path-to specifiers. I.e.
// import ( "foo"; x "bar"; y "foo"; )
// => map{
//     "bar": set{"x"},
//     "foo": set{"", "y"}
//   }
using ImportSpecs = std::set<ImportSpec,ImportSpecLess>;
using Imports     = std::map<std::string,ImportSpecs>;
