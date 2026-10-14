// Semantics POC — INGESTED FIXTURE (M4). Interpreter: mod_sem_poc_kernel.ls;
// lowering: mod_sem_poc_ingest.ls. Design: vibe/Lambda_Semantics_DSL_POC.md §6.3.
//
// The other fixtures hand-translate programs into terms. This one takes the
// production parser's own account of test/lambda/closure.ls — its AST dump,
// committed beside this file as closure.ast.mark — lowers it mechanically and
// checks the model's value of the whole script against the fixture's golden
// (§13.1, the model-vs-golden bucket). No human spelled a term here.

import .mod_sem_poc_kernel
import .mod_sem_poc_ingest

let dump = input("test/lambda/sem/closure.ast.mark", 'mark') or null
let term = normalize(dump) ^ { <unsupported kind: "normalize", why: string(^)> }
let golden = input("test/lambda/closure.txt", 'mark') or null

// how many lowered nodes fell outside the modelled core
fn unsupported_in(t) => if (type(t) != element) 0
  else (if (name(t) == 'unsupported') 1 else 0) + len([for (c in content(t)) for (k in 1 to unsupported_in(c)) k])

// closure.ls lowers entirely into the modelled core
unsupported_in(term) == 0;
// and the model's value of the whole script is the fixture's golden
run(term) == golden;

// the stuck-by-default property survives ingestion: a construct the kernel has
// no rule for (a `match`) lowers to an `unsupported` term, and the run is stuck
let mini = <ast-dump "lambda" <AST_SCRIPT <child <AST_NODE_CONTENT <declare> <item <AST_NODE_MATCH_EXPR>>>>>>
let stuck_term = normalize(mini) ^ { null };
unsupported_in(stuck_term) == 1;
run(stuck_term) == 'stuck'
