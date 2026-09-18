#pragma once

// Tier 3: the WRITE SET (PTH60v3-PTH63v2, PTH71v2).
//
// A CRUD statement does not change anything a reader can see. It appends an
// edit to a write set, and `commit` applies the whole set at once and swaps it
// in as the new head. Two consequences the rest of the design leans on:
//
//   * the next version is WRITE-ONLY (PTH61). Every value operand reads the
//     head, so `put doc#n = doc#n + 1` twice in one set leaves `n + 1`.
//     Read-your-writes lives in Tier 2 (`var`), not here.
//
//   * because a version is an immutable value, a Tier-1 loop can never observe
//     a half-built next version, and the update-while-iterating hazard cannot
//     arise (PTH64v2, MVCC). Nothing here has to know about cursors.
//
// Edits are HEAD-ANCHORED (PTH71v2): the log holds the head node pointer, not a
// position, so `for (v in doc#items) { put x before v }` means what it says
// however the sequence has shifted by the time the set is applied.

// lambda-data.hpp includes lambda.h inside its extern "C" block; including
// lambda.h directly first would win the guard and give the runtime helpers
// C++ linkage, which then fails to link against their C definitions.
#include "../lambda-data.hpp"

typedef enum WriteOp {
    WRITE_OP_PUT = 0,     // `put target = v`   — upsert at a location
    WRITE_OP_BEFORE,      // `put v before t`   — insert before a head node
    WRITE_OP_AFTER,       // `put v after t`    — insert after a head node
    WRITE_OP_INTO,        // `put v into t`     — add as a member
    WRITE_OP_DELETE,      // `del t`            — remove
    // `commit` and `rollback` are transaction statements, not edits. They share
    // the CRUD statement node, so they take values past WriteOp's range: a
    // missing arm can then never read as an edit.
    CRUD_OP_COMMIT = 64,
    CRUD_OP_ROLLBACK,
    // A comma-joined statement (PTH60v3). It holds the clause chain and records
    // one entry per clause in written order, exactly as separate statements
    // would. It exists because appending a bare chain to the enclosing content
    // list truncates it at the head.
    CRUD_OP_SEQUENCE,
} WriteOp;

// One recorded edit. `anchor` is the head node the edit is relative to and
// `key` the step within it, so an edit survives every shift an earlier edit in
// the same set causes.
typedef struct WriteEdit {
    WriteOp op;
    const void* anchor;      // head container the edit is anchored to
    // The edit OWNS its key name. Borrowing the chars of the key Item the
    // statement produced would leave a pointer into collectable memory that
    // the log is not a root for, and the name would be poisoned by the time
    // `commit` read it.
    char* key_name;          // NameKey within the anchor, else NULL
    size_t key_name_length;
    int64_t key_index;       // IntKey, valid when key_name is NULL
    bool has_key;            // false when the target IS the anchor
    Item value;              // the value operand; ItemNull for `del`
    struct Document* doc;    // owning document, for the head swap at commit
} WriteEdit;

// PTH63v2: inside `open { }` every CRUD statement across all documents forms
// ONE write set; outside one, each statement is a one-statement transaction
// that autocommits at once — the shell case, with no pending state to remember.
// PTH78: the scope is DYNAMIC, so a `pn` called from inside the block
// contributes to the block's set, as a stored procedure joins its caller's
// transaction.
bool write_set_transaction_open(void);
void write_set_begin_transaction(void);

// PTH80: an `open` block confines CRUD targets to the opened document. NULL
// means "no confinement", which is the state outside any block.
void write_set_set_confinement(struct Document* doc);
struct Document* write_set_confinement(void);

// Append one edit. Returns false and sets a runtime error when the target is
// outside the open block's confinement.
bool write_set_record(const WriteEdit* edit);

// PTH62: `commit` makes the next version the head and mints the generation;
// `rollback` discards it. Both raise when no transaction is open (PTH78).
// `commit` applies the log in PROGRAM ORDER and validates key domains then
// (S9.1.6, PTH63v2): the first rejection rolls the whole set back and raises.
bool write_set_commit(void);
void write_set_rollback(void);

// Block exit (PTH66v2): commit unless the block is unwinding, in which case an
// unhandled runtime error ALWAYS rolls back.
bool write_set_end_transaction(bool unwinding);

// Cleared with the document context; a write set never outlives its evaluation.
void write_set_reset(void);
