// Tier 3: the write set and the transaction. See write_set.hpp for the model.

#include "write_set.hpp"
#include "doc_context.hpp"
#include "lambda-error.h"

#include "transpiler.hpp"
#include "../lambda.hpp"
#include "../../lib/arraylist.h"
#include "../../lib/log.h"
#include "lambda-number-runtime.hpp"
#include "lambda-root-frame.hpp"
// The write set outlives its statements, so its recorded values are registered
// roots rather than stack-frame ones.
#include "heap_api.h"

// The whole transaction state. It is evaluation-scoped and single-threaded by
// the same invariant as the document context: one thread of control owns the
// cursor and the committer, which is exactly why PTH64v2 can rule that a commit
// never waits (a wait would deadlock).
static ArrayList* g_edits = NULL;       // WriteEdit*, in PROGRAM ORDER
static bool g_transaction_open = false;
static Document* g_confinement = NULL;

bool write_set_transaction_open(void) { return g_transaction_open; }

void write_set_begin_transaction(void) { g_transaction_open = true; }

void write_set_set_confinement(Document* doc) { g_confinement = doc; }

Document* write_set_confinement(void) { return g_confinement; }

static void write_set_clear(void) {
    if (!g_edits) return;
    for (int i = 0; i < g_edits->length; i++) {
        WriteEdit* edit = (WriteEdit*)g_edits->data[i];
        heap_unregister_gc_root(&edit->value.item);
        mem_free(edit->key_name);
        mem_free(edit);
    }
    arraylist_free(g_edits);
    g_edits = NULL;
}

bool write_set_record(const WriteEdit* edit) {
    if (!edit || !edit->doc) {
        set_runtime_error(ERR_TYPE_MISMATCH,
            "CRUD target has no location; only a document node can be written");
        return false;
    }
    // PTH80: inside a block every target must lie within the opened document.
    // Checked here rather than at commit so the diagnostic names the statement.
    if (g_confinement && edit->doc != g_confinement) {
        set_runtime_error(ERR_SEMANTIC_ERROR,
            "CRUD target lies outside the opened document");
        return false;
    }
    if (!g_edits) g_edits = arraylist_new(8);
    WriteEdit* owned = (WriteEdit*)mem_calloc(1, sizeof(WriteEdit), MEM_CAT_EVAL);
    if (!owned) return false;
    *owned = *edit;
    if (edit->key_name) {
        owned->key_name = (char*)mem_calloc(1, edit->key_name_length + 1, MEM_CAT_EVAL);
        if (!owned->key_name) { mem_free(owned); return false; }
        memcpy(owned->key_name, edit->key_name, edit->key_name_length);
    }
    // The recorded value outlives its statement -- inside `open` it waits until
    // the block's commit -- and the log is not otherwise reachable from the
    // collector, so each entry's value is a root until the set is applied.
    heap_register_gc_root(&owned->value.item);
    arraylist_append(g_edits, owned);
    // PTH63v2: outside any `open`, a CRUD statement is a one-statement
    // transaction that autocommits at once — the shell case, with no pending
    // state to remember.
    if (!g_transaction_open) return write_set_commit();
    return true;
}

// ---------------------------------------------------------------------------
// Applying the log
// ---------------------------------------------------------------------------

// The key chain from a document root down to `container`, root-first. This is
// the same walk `&` does; it is what lets an edit recorded against a head node
// be applied to the version being built (PTH71v2).
enum { WRITE_SET_MAX_DEPTH = 256 };
typedef struct AnchorChain {
    const DocNodeEntry* steps[WRITE_SET_MAX_DEPTH];
    int count;
} AnchorChain;

static bool anchor_chain_of(const void* container, AnchorChain* out) {
    out->count = 0;
    for (const DocNodeEntry* walk = doc_context_find_node(container);
            walk && walk->parent; walk = doc_context_find_node(walk->parent)) {
        if (out->count >= WRITE_SET_MAX_DEPTH) return false;
        out->steps[out->count++] = walk;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Immutable container edits
// ---------------------------------------------------------------------------
// The runtime's own setters (fn_map_set, fn_array_set) MUTATE IN PLACE, which
// is right for Tier 2 under COW but wrong here: PTH61 requires the head to stay
// readable and unchanged until `commit` swaps. So every edit below builds a NEW
// container and shares the children it does not touch.

// The empty shape a freshly built map starts from. Fields are then added with
// fn_map_set, which grows the shape — safe because the map is not yet published
// and nothing else can see it.
static Item new_empty_map(void) {
    Pool* pool = context ? context->pool : NULL;
    TypeMap* shape = pool
        ? (TypeMap*)alloc_type(pool, LMD_TYPE_MAP, sizeof(TypeMap)) : NULL;
    Map* result = shape ? map_alloc_for_type(shape, NULL, 0) : NULL;
    if (!result) return ItemError;
    return (Item){.item = (uint64_t)(uintptr_t)result};
}

// Copy `source`'s attribute face into `target`, upserting or dropping one key.
// Insertion order is the shape order, so a new key appends (S2.3.1).
static bool copy_attributes(Item target, Item source, const char* name,
        size_t name_length, Item value, bool remove, bool* applied) {
    TypeId tid = get_type_id(source);
    // `fn_map_set` allocates on every field, so the three values that must
    // outlive the loop are rooted: the container being filled, the one being
    // read (its shape pointers are only valid while it is live), and the
    // replacement. The shape walk itself re-reads `source` through the roots.
    RootFrame roots(3);
    Rooted<Item> rooted_target(roots, target);
    Rooted<Item> rooted_source(roots, source);
    Rooted<Item> rooted_value(roots, value);
    TypeMap* shape = lambda_attr_shape(tid, (const void*)(uintptr_t)rooted_source.get().item);
    if (shape) {
        for (ShapeEntry* field = shape->shape; field; field = field->next) {
            if (field->byte_offset < 0 || !field->name) continue;
            bool hit = name && field->name->length == name_length &&
                memcmp(field->name->str, name, name_length) == 0;
            if (hit && remove) { *applied = true; continue; }
            void* data = lambda_attr_data(tid, (const void*)(uintptr_t)rooted_source.get().item);
            if (!data) break;
            Item field_value = hit ? rooted_value.get() : _map_read_field(field, data);
            if (hit) *applied = true;
            Item key = {.item = s2it(heap_create_name(field->name->str, field->name->length))};
            if (item_is_error(fn_map_set(rooted_target.get(), key, field_value))) return false;
        }
    }
    if (name && !*applied && !remove) {
        Item key = {.item = s2it(heap_create_name(name, name_length))};
        if (item_is_error(fn_map_set(rooted_target.get(), key, rooted_value.get()))) return false;
        *applied = true;
    }
    return true;
}

// Upsert or drop one NAME key, answering a new map/element.
static Item container_with_name(Item source, const char* name,
        size_t name_length, Item value, bool remove) {
    TypeId tid = get_type_id(source);
    RootFrame roots(3);
    Rooted<Item> rooted_source(roots, source);
    Rooted<Item> rooted_value(roots, value);
    Rooted<Item> rooted_result(roots, ItemNull);
    if (tid == LMD_TYPE_MAP) {
        rooted_result.set(new_empty_map());
        Item result = rooted_result.get();
        if (item_is_error(result)) return result;
        bool applied = false;
        if (!copy_attributes(result, rooted_source.get(), name, name_length,
                rooted_value.get(), remove, &applied)) {
            return ItemError;
        }
        result = rooted_result.get();
        if (remove && !applied) {
            // PTH70v4: `del` of an absent location raises at commit.
            set_runtime_error(ERR_SEMANTIC_ERROR,
                "'del' target key is absent from the head");
            return ItemError;
        }
        return result;
    }
    if (tid == LMD_TYPE_ELEMENT) {
        TypeElmt* source_type = (TypeElmt*)((Element*)rooted_source.get().container)->type;
        Pool* pool = context ? context->pool : NULL;
        TypeElmt* shape = pool
            ? (TypeElmt*)alloc_type(pool, LMD_TYPE_ELEMENT, sizeof(TypeElmt)) : NULL;
        if (!shape) return ItemError;
        // The tag is identity, not content: a rebuilt element is the same tag
        // with a new attribute face.
        shape->name = source_type->name;
        shape->name_id = source_type->name_id;
        shape->ns = source_type->ns;
        Element* built = elmt_with_type(shape);
        if (!built) return ItemError;
        rooted_result.set((Item){.item = (uint64_t)(uintptr_t)built});
        bool applied = false;
        if (!copy_attributes(rooted_result.get(), rooted_source.get(), name,
                name_length, rooted_value.get(), remove, &applied)) {
            return ItemError;
        }
        if (remove && !applied) {
            set_runtime_error(ERR_SEMANTIC_ERROR,
                "'del' target key is absent from the head");
            return ItemError;
        }
        Element* source_element = (Element*)rooted_source.get().container;
        for (int64_t i = 0; i < source_element->length; i++) {
            list_push((List*)rooted_result.get().container, source_element->items[i]);
            source_element = (Element*)rooted_source.get().container;
        }
        return rooted_result.get();
    }
    set_runtime_error(ERR_TYPE_MISMATCH,
        "a name target needs a map or element, got type: %s", get_type_name(tid));
    return ItemError;
}

// Insert `value` into a sequence face at `index`, replace the element there, or
// remove it. A positional insert changes every later index, so the sequence is
// rebuilt rather than patched.
typedef enum { SPLICE_INSERT, SPLICE_REPLACE, SPLICE_REMOVE } SpliceKind;

static Item sequence_splice(Item seq, int64_t index, Item value, SpliceKind kind) {
    TypeId tid = get_type_id(seq);
    bool is_element = tid == LMD_TYPE_ELEMENT;
    // A numeric array is a REPRESENTATION of a sequence (S1.6), so an edit may
    // leave it holding mixed kinds; the result is a plain array, read through
    // the generic accessors so the source's lane packing stays private here.
    bool is_numeric = tid == LMD_TYPE_ARRAY_NUM;
    if (tid != LMD_TYPE_ARRAY && !is_element && !is_numeric) {
        set_runtime_error(ERR_TYPE_MISMATCH,
            "a positional target needs a sequence or element, got type: %s",
            get_type_name(tid));
        return ItemError;
    }
    int64_t length = is_numeric ? array_num_iter_count(seq.array_num)
                                : ((List*)seq.container)->length;
    // S9.1.6: `index == length` APPENDS for a replace; an insert may also sit
    // at the end; a remove needs an element that exists.
    bool past_end = index == length;
    if (index < 0 || index > length || (kind == SPLICE_REMOVE && past_end)) {
        set_runtime_error(ERR_INDEX_OUT_OF_BOUNDS,
            "CRUD target position %lld is outside the head sequence",
            (long long)index);
        return ItemError;
    }
    // `list_push` allocates on growth, so the three values that outlive the
    // copy loop are rooted: the sequence being read (its `items` pointer is
    // only valid while it is live), the container being filled, and the
    // inserted value (rule 15, D5.3.3).
    RootFrame roots(3);
    Rooted<Item> rooted_seq(roots, seq);
    Rooted<Item> rooted_value(roots, value);
    Rooted<Item> rooted_result(roots, ItemNull);
    if (is_element) {
        Item rebuilt = container_with_name(rooted_seq.get(), NULL, 0, ItemNull, false);
        if (item_is_error(rebuilt)) return rebuilt;
        // container_with_name copied the children too; drop them and refill.
        rooted_result.set(rebuilt);
        ((List*)rebuilt.container)->length = 0;
    } else {
        Array* array = array_plain();
        if (!array) return ItemError;
        rooted_result.set((Item){.item = (uint64_t)(uintptr_t)array});
    }
    for (int64_t i = 0; i < length; i++) {
        List* result = (List*)rooted_result.get().container;
        if (i == index) {
            if (kind == SPLICE_REMOVE) continue;
            list_push(result, rooted_value.get());
            result = (List*)rooted_result.get().container;
            if (kind == SPLICE_REPLACE) continue;
        }
        Item element = is_numeric
            ? array_num_get(rooted_seq.get().array_num, i)
            : ((List*)rooted_seq.get().container)->items[i];
        list_push(result, element);
    }
    if (past_end && kind != SPLICE_REMOVE) {
        list_push((List*)rooted_result.get().container, rooted_value.get());
    }
    return rooted_result.get();
}

// `put v into t` (PTH70v4): sequence appends, a map value upserts each key, an
// element takes a map into its attributes and anything else as a child. The
// value's kind selecting an element's face is the rule the `<T attrs, content>`
// literal already uses (S2.1.3).
static Item container_add_member(Item target, Item value) {
    TypeId tid = get_type_id(target);
    if (tid == LMD_TYPE_ARRAY) {
        return sequence_splice(target, target.array->length, value, SPLICE_INSERT);
    }
    // A map value upserts each of its keys — into a map, and into an element's
    // ATTRIBUTE face. Any other value appends as an element child. The kind
    // selecting the face is the rule `<T attrs, content>` already uses.
    bool merge_attributes = get_type_id(value) == LMD_TYPE_MAP &&
        (tid == LMD_TYPE_MAP || tid == LMD_TYPE_ELEMENT);
    if (merge_attributes) {
        Item result = target;
        TypeMap* shape = lambda_attr_shape(LMD_TYPE_MAP, (const void*)(uintptr_t)value.item);
        void* data = lambda_attr_data(LMD_TYPE_MAP, (const void*)(uintptr_t)value.item);
        if (!shape || !data) return result;
        for (ShapeEntry* field = shape->shape; field; field = field->next) {
            if (field->byte_offset < 0 || !field->name) continue;
            result = container_with_name(result, field->name->str,
                field->name->length, _map_read_field(field, data), false);
            if (item_is_error(result)) return result;
        }
        return result;
    }
    if (tid == LMD_TYPE_ELEMENT) {
        return sequence_splice(target, (int64_t)target.element->length, value,
            SPLICE_INSERT);
    }
    set_runtime_error(ERR_TYPE_MISMATCH,
        "'into' needs a container target (sequence, map, or element), got type: %s",
        get_type_name(tid));
    return ItemError;
}

// Where `child` currently sits inside the sequence face of `container`, or -1.
// PTH71v2 makes this the whole point of head anchoring: an edit names a NODE,
// and its position may have shifted since it was recorded — by an earlier edit
// in this same set, or by a commit that happened mid-loop.
static int64_t locate_child(Item container, const void* child) {
    TypeId tid = get_type_id(container);
    int64_t length = tid == LMD_TYPE_ARRAY ? container.array->length
        : tid == LMD_TYPE_ELEMENT ? (int64_t)container.element->length
        : tid == LMD_TYPE_ARRAY_NUM ? array_num_iter_count(container.array_num) : 0;
    for (int64_t i = 0; i < length; i++) {
        Item item = tid == LMD_TYPE_ARRAY_NUM ? array_num_get(container.array_num, i)
            : tid == LMD_TYPE_ARRAY ? array_get(container.array, i)
                                    : container.element->items[i];
        if ((const void*)(uintptr_t)item.item == child) return i;
    }
    return -1;
}

// Walk the version being built down to the anchor, recording each container and
// the key that reaches the next one. A NAME key is stable, so it is used as
// recorded; a POSITION is resolved by identity, which is what keeps
// `for (v in doc#items) { put x before v; put v.a = 1 }` meaning what it says.
static bool descend_to_anchor(Item root, const AnchorChain* chain, int depth,
        Item* nodes, Item* keys) {
    nodes[0] = root;
    for (int i = 0; i < depth; i++) {
        const DocNodeEntry* step = chain->steps[chain->count - 1 - i];
        if (step->key_name) {
            keys[i] = (Item){.item = s2it(heap_create_name(step->key_name,
                step->key_name_length))};
        } else {
            int64_t index = locate_child(nodes[i], step->container);
            if (index < 0) {
                // PTH71v2: a node a later commit REPLACED is no longer in the
                // head, and an edit anchored to it is a stale-anchor conflict —
                // the same optimistic first-committer-wins rule the store
                // applies between evaluations (PTH67v2).
                set_runtime_error(ERR_SEMANTIC_ERROR,
                    "stale-anchor conflict: the CRUD target's node is no longer "
                    "in the head; it was replaced by an earlier commit");
                return false;
            }
            keys[i] = (Item){.item = i2it((int32_t)index)};
        }
        nodes[i + 1] = fn_index(nodes[i], keys[i]);
        if (item_is_error(nodes[i + 1])) return false;
        if (get_type_id(nodes[i + 1]) == LMD_TYPE_NULL) {
            set_runtime_error(ERR_SEMANTIC_ERROR,
                "CRUD target location is absent from the head");
            return false;
        }
    }
    return true;
}

// Put `replacement` back where `nodes[depth]` was, rebuilding the spine above
// it. Nodes off the path are SHARED, not copied — that is the structural
// sharing that makes an untouched subtree the SAME node across a commit.
static Item rebuild_spine(const Item* nodes, const Item* keys, int depth,
        Item replacement) {
    Item current = replacement;
    for (int i = depth - 1; i >= 0; i--) {
        TypeId key_tid = get_type_id(keys[i]);
        if (is_text_type_id(key_tid)) {
            current = container_with_name(nodes[i], keys[i].get_chars(),
                keys[i].get_len(), current, false);
        } else {
            int64_t index = 0;
            lambda_item_to_int64_exact(keys[i], &index);
            current = sequence_splice(nodes[i], index, current, SPLICE_REPLACE);
        }
        if (item_is_error(current)) return current;
    }
    return current;
}

// Apply one edit to the version under construction, answering the new root.
static Item apply_edit(Item root, const WriteEdit* edit) {
    AnchorChain chain;
    if (!anchor_chain_of(edit->anchor, &chain)) {
        set_runtime_error(ERR_SEMANTIC_ERROR, "CRUD target is nested too deeply");
        return ItemError;
    }
    // Two edit shapes stop one level short of the anchor and act on the
    // container it sits in: `before`/`after`, and any edit whose target IS the
    // node (`del v`, `put v = x`). Both then use the anchor's key as RESOLVED
    // against the version being built, never as recorded.
    bool positional = edit->op == WRITE_OP_BEFORE || edit->op == WRITE_OP_AFTER;
    // `into` adds a MEMBER to the anchor, so unlike the other key-less forms it
    // acts on the anchor itself rather than on the container holding it.
    bool node_target = positional ||
        (!edit->has_key && edit->op != WRITE_OP_INTO);
    if (positional && chain.count == 0) {
        set_runtime_error(ERR_TYPE_MISMATCH,
            "'before' and 'after' need a node inside a sequence; a document "
            "root has no position");
        return ItemError;
    }
    // A document root has no parent, so `put root = v` replaces the whole
    // document rather than a key inside one.
    if (node_target && chain.count == 0) return edit->value;
    int depth = node_target ? chain.count - 1 : chain.count;

    // The descent's containers and keys are read again on the way back up,
    // across `container_with_name`/`sequence_splice` calls that allocate, so
    // they are rooted for this activation. One span covers both arrays: the
    // node at `i` and the key that reaches the node at `i + 1`.
    RootSpan descent(2 * (size_t)(chain.count + 1));
    Item* nodes = descent.items();
    Item* keys = nodes + chain.count + 1;
    if (!descend_to_anchor(root, &chain, chain.count, nodes, keys)) return ItemError;

    Item replacement = ItemNull;
    if (positional) {
        int64_t at = 0;
        lambda_item_to_int64_exact(keys[depth], &at);
        if (is_text_type_id(get_type_id(keys[depth]))) {
            set_runtime_error(ERR_TYPE_MISMATCH,
                "'before' and 'after' need a position; a name target has none — "
                "use 'put t = v' to upsert a key");
            return ItemError;
        }
        if (edit->op == WRITE_OP_AFTER) at++;
        replacement = sequence_splice(nodes[depth], at, edit->value, SPLICE_INSERT);
    } else if (edit->op == WRITE_OP_INTO) {
        replacement = container_add_member(nodes[depth], edit->value);
    } else {
        // The key the edit acts through: the recorded one for an explicit
        // location (`put doc#items[3] = v` names position 3, PTH70v4), or the
        // anchor's own resolved key when the target is the node itself.
        const char* name = edit->has_key ? edit->key_name : NULL;
        size_t name_length = edit->key_name_length;
        int64_t index = edit->key_index;
        if (!edit->has_key) {
            Item key = keys[depth];
            if (is_text_type_id(get_type_id(key))) {
                name = key.get_chars();
                name_length = key.get_len();
            } else {
                name = NULL;
                lambda_item_to_int64_exact(key, &index);
            }
        }
        bool remove = edit->op == WRITE_OP_DELETE;
        replacement = name
            ? container_with_name(nodes[depth], name, name_length,
                remove ? ItemNull : edit->value, remove)
            : sequence_splice(nodes[depth], index,
                remove ? ItemNull : edit->value,
                remove ? SPLICE_REMOVE : SPLICE_REPLACE);
    }
    if (item_is_error(replacement)) return replacement;
    return rebuild_spine(nodes, keys, depth, replacement);
}

bool write_set_commit(void) {
    if (!g_edits || !g_edits->length) { write_set_clear(); return true; }
    // PTH63v2: applied in PROGRAM ORDER and validated THEN — the statement site
    // cannot read the next version, and an earlier statement may have created
    // the parent. On the first rejection nothing is written and the whole set
    // rolls back.
    // One `open` block may span documents (PTH63v2), so the log is applied per
    // document. The whole set is still atomic: a rejection anywhere discards
    // every working version and leaves every head untouched.
    enum { MAX_DOCUMENTS_PER_COMMIT = 32 };
    Document* docs[MAX_DOCUMENTS_PER_COMMIT];
    int doc_count = 0;
    // Each `apply_edit` allocates, so every half-built version has to survive a
    // collection until it is swapped in as a head. RootSpan is the precise
    // owner for an Item span bounded by one native activation (rule 15).
    RootSpan working_span(MAX_DOCUMENTS_PER_COMMIT);
    Item* working = working_span.items();
    for (int i = 0; i < g_edits->length; i++) {
        WriteEdit* edit = (WriteEdit*)g_edits->data[i];
        int slot = -1;
        for (int d = 0; d < doc_count; d++) if (docs[d] == edit->doc) { slot = d; break; }
        if (slot < 0) {
            if (doc_count >= MAX_DOCUMENTS_PER_COMMIT) {
                set_runtime_error(ERR_SEMANTIC_ERROR,
                    "a transaction may span at most %d documents",
                    MAX_DOCUMENTS_PER_COMMIT);
                write_set_rollback();
                return false;
            }
            slot = doc_count++;
            docs[slot] = edit->doc;
            working[slot] = edit->doc->head;
        }
        Item next = apply_edit(working[slot], edit);
        if (item_is_error(next)) { write_set_rollback(); return false; }
        working[slot] = next;
    }
    // PTH43v2: the generation is minted HERE, and only here — a commit always
    // creates new nodes, which is what makes `===` sound as pointer equality.
    for (int d = 0; d < doc_count; d++) doc_context_commit_head(docs[d], working[d]);
    write_set_clear();
    return true;
}

void write_set_rollback(void) { write_set_clear(); }

bool write_set_end_transaction(bool unwinding) {
    // PTH66v2: an `open` block commits explicitly or implicitly at its end, and
    // ALWAYS rolls back if it exits by an unhandled runtime error.
    bool ok = true;
    if (unwinding) write_set_rollback();
    else ok = write_set_commit();
    g_transaction_open = false;
    g_confinement = NULL;
    return ok;
}

void write_set_reset(void) {
    write_set_clear();
    g_transaction_open = false;
    g_confinement = NULL;
}

// ---------------------------------------------------------------------------
// The CRUD statement entry points
// ---------------------------------------------------------------------------
// PTH69v2: a CRUD target is a reference with `#` (post-`#` steps re-appended),
// an `open` alias, or a Tier-1 binding of a head node plus member steps. All
// three arrive here as a head CONTAINER plus an optional key, because that is
// what a head-anchored edit needs (PTH71v2).

// Resolve a container to its document, or report why it has none.
static Document* target_document(Item container, const char* what) {
    const DocNodeEntry* entry = doc_context_find_node(
        (const void*)(uintptr_t)container.item);
    if (!entry || !entry->doc) {
        set_runtime_error(ERR_TYPE_MISMATCH,
            "%s target has no location: only a document node can be written; "
            "a runtime value or a local copy carries no identity", what);
        return NULL;
    }
    return entry->doc;
}

static bool record_keyed(WriteOp op, Item base, Item key, Item value,
        const char* what) {
    Document* doc = target_document(base, what);
    if (!doc) return false;
    WriteEdit edit = {};
    edit.op = op;
    edit.anchor = (const void*)(uintptr_t)base.item;
    edit.value = value;
    edit.doc = doc;
    edit.has_key = true;
    TypeId key_tid = get_type_id(key);
    if (is_text_type_id(key_tid) && key.get_chars()) {
        // Borrowed only until write_set_record copies it.
        edit.key_name = (char*)key.get_chars();
        edit.key_name_length = key.get_len();
    } else {
        int64_t index = 0;
        if (!lambda_item_to_int64_exact(key, &index)) {
            set_runtime_error(ERR_TYPE_MISMATCH,
                "%s target key must be a name or an exact non-negative integer", what);
            return false;
        }
        edit.key_index = index;
    }
    return write_set_record(&edit);
}

// `put base[key] = value` / `del base[key]`.
extern "C" Item fn_put_member(Item base, Item key, Item value) {
    return record_keyed(WRITE_OP_PUT, base, key, value, "put") ? ItemNull : ItemError;
}

extern "C" Item fn_del_member(Item base, Item key) {
    return record_keyed(WRITE_OP_DELETE, base, key, ItemNull, "del") ? ItemNull : ItemError;
}

// `put node = value` and `del node`: the target IS a head node, so the edit is
// recorded against its PARENT at the node's own key — replacing or removing a
// node is an edit to where it sits.
static bool record_node(WriteOp op, Item node, Item value, const char* what) {
    const DocNodeEntry* entry = doc_context_find_node(
        (const void*)(uintptr_t)node.item);
    if (!entry || !entry->doc) {
        set_runtime_error(ERR_TYPE_MISMATCH,
            "%s target has no location: only a document node can be written; "
            "a runtime value or a local copy carries no identity", what);
        return false;
    }
    if (!entry->parent && op == WRITE_OP_DELETE) {
        set_runtime_error(ERR_SEMANTIC_ERROR,
            "'del' cannot remove a document root");
        return false;
    }
    // PTH71v2: anchor to the NODE, never to the key it had when the statement
    // ran. `apply_edit` resolves where the node sits in the version being
    // built, so a position an earlier edit shifted is still named correctly.
    WriteEdit edit = {};
    edit.op = op;
    edit.value = value;
    edit.doc = entry->doc;
    edit.anchor = entry->container;
    edit.has_key = false;
    return write_set_record(&edit);
}

extern "C" Item fn_put_node(Item node, Item value) {
    return record_node(WRITE_OP_PUT, node, value, "put") ? ItemNull : ItemError;
}

extern "C" Item fn_del_node(Item node) {
    return record_node(WRITE_OP_DELETE, node, ItemNull, "del") ? ItemNull : ItemError;
}

// `put v before t` / `put v after t` (PTH70v4): `t` is a head NODE and the edit
// is anchored to the sequence it sits in, at its position — so two inserts on
// one node land in program order however the sequence has shifted (PTH71v2).
static bool record_positional(WriteOp op, Item value, Item node) {
    const DocNodeEntry* entry = doc_context_find_node(
        (const void*)(uintptr_t)node.item);
    if (!entry || !entry->doc || !entry->parent) {
        set_runtime_error(ERR_TYPE_MISMATCH,
            "'before' and 'after' need a document node inside a sequence");
        return false;
    }
    if (entry->key_name) {
        set_runtime_error(ERR_TYPE_MISMATCH,
            "'before' and 'after' need a position; a name target has none - "
            "use 'put t = v' to upsert a key");
        return false;
    }
    // PTH71v2: the anchor is the NODE. Its position is resolved against the
    // version being built, at commit, so two inserts on one node land in
    // program order however the sequence has shifted by then.
    WriteEdit edit = {};
    edit.op = op;
    edit.anchor = entry->container;
    edit.value = value;
    edit.doc = entry->doc;
    return write_set_record(&edit);
}

extern "C" Item fn_put_before(Item value, Item node) {
    return record_positional(WRITE_OP_BEFORE, value, node) ? ItemNull : ItemError;
}

extern "C" Item fn_put_after(Item value, Item node) {
    return record_positional(WRITE_OP_AFTER, value, node) ? ItemNull : ItemError;
}

// `put v into t` (PTH70v4): `into` is total on containers and is what replaces
// `push` in Tier 3.
extern "C" Item fn_put_into(Item value, Item container) {
    Document* doc = target_document(container, "put ... into");
    if (!doc) return ItemError;
    WriteEdit edit = {};
    edit.op = WRITE_OP_INTO;
    edit.anchor = (const void*)(uintptr_t)container.item;
    edit.value = value;
    edit.doc = doc;
    return write_set_record(&edit) ? ItemNull : ItemError;
}

// PTH78: `commit`/`rollback` with no transaction open RAISE. Outside `open`
// every statement has already committed, so reaching one there is a mistake the
// programmer should hear about.
extern "C" Item fn_commit(void) {
    if (!write_set_transaction_open()) {
        set_runtime_error(ERR_SEMANTIC_ERROR,
            "'commit' with no transaction open; outside 'open' every CRUD "
            "statement commits on its own");
        return ItemError;
    }
    if (!write_set_commit()) return ItemError;
    return ItemNull;
}

extern "C" Item fn_rollback(void) {
    if (!write_set_transaction_open()) {
        set_runtime_error(ERR_SEMANTIC_ERROR,
            "'rollback' with no transaction open; outside 'open' every CRUD "
            "statement commits on its own");
        return ItemError;
    }
    write_set_rollback();
    return ItemNull;
}

// ---------------------------------------------------------------------------
// `open target { … }` (PTH68v3, PTH75v3, PTH80)
// ---------------------------------------------------------------------------
// The block form of the S14 scoped resource: its exit is a commit-or-rollback
// point (PTH66v2), a `commit` inside ends the current write set and begins a
// new one, and the scope is DYNAMIC (PTH78) — a `pn` called from inside joins
// the block's write set, as a stored procedure joins its caller's transaction.
//
// The alias is a reference with `#` implied at the end of every navigation
// chain through it (PTH75v3), which at the runtime boundary simply means the
// block binds the FORCED target.

extern "C" Item fn_open_begin(Item target) {
    // PTH-O15: nesting is deferred, so an inner `open` is an error rather than
    // a silently flattened savepoint.
    if (write_set_transaction_open()) {
        set_runtime_error(ERR_SEMANTIC_ERROR,
            "nested 'open' is not supported yet; commit the inner work first");
        return ItemError;
    }
    // PTH75v3: `#` is implied on an `open` target, so the alias is the opened
    // DOCUMENT, not the lazy address. `&v` recovers the address.
    Item opened = get_type_id(target) == LMD_TYPE_PATH ? fn_force(target) : target;
    if (item_is_error(opened)) return opened;
    const DocNodeEntry* entry = doc_context_find_node(
        (const void*)(uintptr_t)opened.item);
    if (!entry || !entry->doc) {
        set_runtime_error(ERR_TYPE_MISMATCH,
            "'open' needs a document, a document set, or a directory");
        return ItemError;
    }
    write_set_begin_transaction();
    // PTH80: every CRUD target inside the block must lie within the opened
    // document. Checked at the statement so the diagnostic names it.
    write_set_set_confinement(entry->doc);
    return opened;
}

extern "C" Item fn_open_end(Bool unwinding) {
    // PTH66v2: commit at the end, and ALWAYS roll back when the block exits by
    // an unhandled runtime error.
    return write_set_end_transaction(unwinding == BOOL_TRUE) ? ItemNull : ItemError;
}
