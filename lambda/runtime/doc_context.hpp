#pragma once

// The per-evaluation DOCUMENT CONTEXT (PTH43v2, PTH50v3, DO25).
//
// Two things live here, and they are the same thing seen from two ends:
//
//   * a table `location -> (head root, generation counter)`, so `#` binds a
//     location to ONE head value for the evaluation. Without it two forces of
//     the same location would load twice and `p#body.0 === p#body.0` would be
//     false, when PTH45v2 requires true.
//
//   * a NODE TABLE keyed by container pointer giving `(document, parent, key,
//     generation)`. That is the identity carrier DO25 asked for: document
//     nodes are Input-arena owned, immutable and non-moving (D4.3.1), so the
//     container pointer identifies the node and nothing has to be stored in
//     the node itself. `&` walks this table to the root and spells the path;
//     `===` is pointer equality on top of it.
//
// Runtime-constructed data is never registered, which is exactly PTH41/PTH52v3:
// it has no identity, `&` answers null, `===` answers false.

// lambda-data.hpp includes lambda.h inside its extern "C" block; including
// lambda.h directly first would win the guard and give the runtime helpers
// C++ linkage, which then fails to link against their C definitions.
#include "../lambda-data.hpp"

struct Document;
struct DocRetainChunk;

// One registered container and the step that reaches it from `parent` — a
// NameKey or an IntKey (S8.2.1v4). The key is held as raw name-plus-index
// rather than as an Item on purpose: this table is not GC-scanned, so it must
// not own a collectable pointer. Shape names are interned for at least the
// document's lifetime, which is exactly this table's lifetime.
typedef struct DocNodeEntry {
    const void* container;
    const void* parent;      // NULL at the document root
    const char* key_name;    // NameKey, else NULL
    size_t key_name_length;
    int64_t key_index;       // IntKey, valid when key_name is NULL
    Document* doc;
    // PTH43v2: stamped by `commit` with the document's next generation. 0 is
    // the generation a freshly loaded head carries. Untouched subtrees keep
    // their stamp across a commit and *are* the same nodes.
    uint32_t generation;
} DocNodeEntry;

struct Document {
    String* location;   // canonical spelling (S2.4.2v4); the context key
    Path* path;         // the location as a path, for `&` to extend
    Item head;          // head root value
    uint32_t generation;
    // PTH67v2: "a version a binding or cursor still holds must stay readable
    // after a later commit". The node table addresses nodes by POINTER, so a
    // superseded version that the collector freed would let a later allocation
    // land on a registered address and inherit a dead node's parent and key.
    // Every committed version is therefore retained as a GC root, per version
    // rather than per node: everything a version's table entries name is
    // reachable from its root. The cost is one retained version per commit for
    // the evaluation — which is what makes `open` the remedy for a loop of
    // writes (PTH64v2), not just an atomicity one.
    DocRetainChunk* retained;   // newest chunk first
};

// Look a location up without loading. NULL when it has never been forced.
Document* doc_context_lookup(const char* location, size_t length);

// Install `head` as the location's head and register every container reachable
// from it. Replaces any existing entry (this is what `commit` does, PTH62).
Document* doc_context_install(Path* path, const char* location, size_t length,
    Item head, uint32_t generation);

// The node table lookup behind `&` and `===`.
const DocNodeEntry* doc_context_find_node(const void* container);

// Register one container under (parent, key) — used by `commit` for the nodes a
// write set created, which is the only other place identity is minted. Pass
// `key_name == NULL` for an IntKey.
void doc_context_register(Document* doc, const void* container,
    const void* parent, const char* key_name, size_t key_name_length,
    int64_t key_index, uint32_t generation);

// Register `root` and everything a navigation can reach from it.
void doc_context_register_tree(Document* doc, Item root, const void* parent,
    const char* key_name, size_t key_name_length, int64_t key_index,
    uint32_t generation);

// PTH62/PTH43v2: make `head` the document's head and stamp the nodes the write
// set created with the next generation. Untouched subtrees keep their stamp and
// *are* the same nodes, which is what makes `===` across an unrelated commit
// still answer true.
void doc_context_commit_head(Document* doc, Item head);

// PTH44v2/SO20: the context lives for the evaluation. Called beside path_reset().
void doc_context_reset(void);
