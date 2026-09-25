// Native Lambda port of test/error_handling/check_recovery_boundaries.py.
fn is_source(name: string) bool =>
    ends_with(name, ".c") or ends_with(name, ".cc") or
    ends_with(name, ".cpp") or ends_with(name, ".h") or ends_with(name, ".hpp")

fn is_vendored(path: string) bool =>
    contains(path, ".mir.") or contains(path, ".tree-sitter")

fn allowed(path: string) bool => contains([
    "\\.lambda.runtime.'concurrency.cpp'",
    "\\.lambda.runtime.'interp.cpp'",
    "\\.lambda.runtime.'lambda-eval.cpp'",
    "\\.lambda.runtime.'lambda-stack.cpp'",
    "\\.lambda.runtime.'recovery_frame.c'",
    "\\.lambda.runtime.'recovery_frame.h'",
    "\\.lambda.runtime.'runner.cpp'",
    "\\.lambda.runtime.'sys_func_registry.c'",
    "\\.lambda.runtime.'transpile-mir.cpp'",
    "\\.lambda.js.'js_mir_entrypoints_require.cpp'",
    "\\.lambda.jube.'jube_registry.cpp'",
    "\\.lambda.'main.cpp'"
], path)

// Match the source gate's optional whitespace between a name and its call.
pn has_call(source: string, name: string) {
    var position = index_of(source, name)
    while (position != null) {
        var tail = position + len(name)
        while (tail < len(source) and contains(" \t\r\n", slice(source, tail, tail + 1))) {
            tail = tail + 1
        }
        if (slice(source, tail, tail + 1) == "(") { return true }
        let next_start = position + len(name)
        let next = index_of(slice(source, next_start), name)
        position = if (next == null) null else next_start + next
    }
    return false
}

pn main() {
    var failures = 0
    for (path in \.lambda.**) {
        let rel = string(path)
        if (path.is_file and is_source(path.name) and not is_vendored(rel)) {
            let source = input(path, "text")^
            let direct = has_call(source, "lambda_recovery_frame_raise_fault") or
                         has_call(source, "lambda_recovery_frame_raise_local_fault")
            let boundary = contains(source, "lambda_recovery_frame_begin_for") or
                           contains(source, "LAMBDA_RECOVERY_FRAME_SETJMP") or
                           has_call(source, "lambda_recovery_frame_arm") or
                           has_call(source, "lambda_recovery_frame_end")
            if (direct and not allowed(rel)) {
                print("error-recovery-gate: " ++ rel ++
                      ": direct recovery call is outside the native-fault allowlist\n")
                failures = failures + 1
            }
            if (boundary and not allowed(rel)) {
                print("error-recovery-gate: " ++ rel ++
                      ": recovery boundary is outside the native-fault allowlist\n")
                failures = failures + 1
            }
            if (contains(source, "LAMBDA_FAULT_EQUALITY_DEPTH_EXHAUSTION")) {
                print("error-recovery-gate: " ++ rel ++
                      ": equality-depth failure must use an ordinary completion\n")
                failures = failures + 1
            }
        }
    }
    if (failures > 0) { raise error("error-recovery-gate: " ++ string(failures) ++ " violation(s)") }
    print("error-recovery-gate: native recovery call sites are allowlisted\n")
}
