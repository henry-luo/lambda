#pragma once

// Ownership-tree levels for Radiant Heap structs.
//
// Each level names its parent level; Process is the root. A Heap struct declares
// the level that holds its storage with LAM_NODE_OF. An Up<T> field is valid
// when the target's level is the holder's level or one of its ancestors. A
// Stack struct is pass-local and may point at any level. Lint checks the
// declared levels; the debug heap tracer checks actual placements.

#include "mem_kind.hpp"

namespace lam {

struct NodeProcess  { typedef void parent; };          // process-wide registries and caches
struct NodeDocument { typedef NodeProcess parent; };    // a document: Input arena, DOM nodes, node arena, document pool
struct NodeViewTree { typedef NodeDocument parent; };   // the document's view tree: props, layout and paint storage
struct NodeDocState { typedef NodeDocument parent; };   // the document's interaction state store
struct NodeStack    { typedef void parent; };           // pass-local structs; not part of the Heap

} // namespace lam

// Declares the level that holds T's storage. Use at global scope after T is declared.
#define LAM_NODE_OF(T, Level) \
    template<> struct lam::NodeOf<T> { typedef lam::Level type; }
