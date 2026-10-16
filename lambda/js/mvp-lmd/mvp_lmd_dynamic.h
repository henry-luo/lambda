#pragma once
#include "mvp_lmd_runtime.h"

// Dynamic source executes only through the retained shared AST interpreter.
Item mvp_lmd_dynamic_initialize(MvpLmdProgram*);
Item mvp_lmd_eval_script(Context*, MvpLmdProgram*, Item*, uint64_t, Item, Item, Item);
Item mvp_lmd_dynamic_lexical(MvpLmdProgram*, Item key, Item value, int operation, bool* found);
Item mvp_lmd_dynamic_function(Context*, MvpLmdProgram*, Item*, uint64_t,
    Item self, Item receiver, Item new_target);
enum MvpLmdDynamicFunctionKind {
    LMD_DYNAMIC_FUNCTION, LMD_DYNAMIC_GENERATOR,
    LMD_DYNAMIC_ASYNC_FUNCTION, LMD_DYNAMIC_ASYNC_GENERATOR
};
// all dynamic kinds share retained-interpreter admission and source conversion.
Item mvp_lmd_dynamic_function_kind(MvpLmdProgram*, Item*, uint64_t,
    Item new_target, MvpLmdDynamicFunctionKind);
Item mvp_lmd_dynamic_wrap_function(MvpLmdProgram*, Item);
Item mvp_lmd_dynamic_unwrap_function(Item);
extern "C" Item mvp_lmd_dynamic_binary(int operation, Item left, Item right);
// preserve native descriptor identity and rebind aliases to the active global.
Item mvp_lmd_import_host_property(MvpLmdProgram*, Item owner, Item key,
    const JsPropertyDescriptor*, Item previous_global = ItemNull);
enum MvpLmdForeignOperation {
    LMD_FOREIGN_GET_PROTOTYPE = 32, LMD_FOREIGN_SET_PROTOTYPE,
    LMD_FOREIGN_EXTENSIBLE, LMD_FOREIGN_PREVENT,
    LMD_FOREIGN_DESCRIPTOR, LMD_FOREIGN_DEFINE, LMD_FOREIGN_PROJECT
};
// false means this is not foreign storage; completion preserves false versus throw.
bool mvp_lmd_foreign_property(Item owner, Item key, Item value, Item receiver,
    int operation, Item* completion, JsPropertyDescriptor* descriptor = NULL);
// Returns undefined for an ordinary MVP callable; dynamic bodies keep their
// exact retained source independently of the original MIR unit's source span.
Item mvp_lmd_dynamic_function_source(Item);
Item mvp_lmd_dynamic_class(MvpLmdProgram*, AstClassNode*, Item base, Item constructor = ItemNull);

// Implemented by the program owner, which owns the actual live global cells.
extern "C" Item mvp_lmd_dynamic_global(MvpLmdProgram*, Item key, Item value,
    int64_t operation, bool strict);
Item mvp_lmd_dynamic_global_this(MvpLmdProgram*, bool required = true);

struct JsScript;
bool mvp_lmd_dynamic_retain_script(MvpLmdProgram*, JsScript*);

struct NameScope;
struct JsInterpEnv;
struct JsInterpHostCaller;
struct JsInterpHostReference;
// Private MIR edges use exact environment pointers, never guest Items.
extern "C" JsInterpEnv* mvp_lmd_dynamic_environment(MvpLmdProgram*, NameScope*,
    JsInterpEnv* outer, int64_t flags);
extern "C" Item mvp_lmd_dynamic_initialize_body(MvpLmdProgram*, JsInterpEnv* body, JsInterpEnv* parameters);
extern "C" Item mvp_lmd_dynamic_eval(MvpLmdProgram*, Item source, const JsInterpHostCaller*);
extern "C" Item mvp_lmd_dynamic_reference(MvpLmdProgram*, const JsInterpHostCaller*,
    JsInterpHostReference*, Item key, Item value, int64_t operation);
