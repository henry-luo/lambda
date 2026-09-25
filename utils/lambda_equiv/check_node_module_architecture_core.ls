// Native source/config core of utils/check_node_module_architecture.py.
import .path_utils

let node_modules = ["node_core", "node_zlib", "node_fs", "node_net",
                    "node_child_process", "node_http", "node_tls", "node_crypto"]
let forbidden_tokens = [
    "\"../../js/", "\"../js/", "\"../../runtime/", "\"../runtime/",
    "js_runtime.h", "js_runtime_state.hpp", "js_globals.cpp",
    "\"../../lambda.hpp\"", "\"../../lambda-data.hpp\"",
    "LambdaRootFrame", "Rooted<", "heap_register_gc_root", "js_heap_epoch",
    "uv_", "<zlib.h>", "<mbedtls/", "<openssl/", "<curl/"
]
let fs_forbidden = [
    "<dirent.h>", "<sys/statvfs.h>", "<unistd.h>", "<io.h>",
    "<direct.h>", "<windows.h>", "NODE_FS_OPEN(", "NODE_FS_CLOSE(",
    "NODE_FS_READ(", "NODE_FS_WRITE(", "NODE_FS_STAT(", "NODE_FS_LSTAT(",
    "NODE_FS_FSTAT(", "NODE_FS_CHMOD(", "NODE_FS_FCHMOD(", "NODE_FS_LINK(",
    "NODE_FS_MKDIR(", "NODE_FS_RMDIR(", "NODE_FS_UNLINK("
]

fn source_file(name: string) bool =>
    ends_with(name, ".c") or ends_with(name, ".cc") or ends_with(name, ".cpp") or
    ends_with(name, ".h") or ends_with(name, ".hpp")

pub pn scan_source(path: string, source: string) {
    var findings = []
    let tokens = if (contains(path, "/node_fs/") or contains(path, ".node_fs."))
                 forbidden_tokens ++ fs_forbidden else forbidden_tokens
    var line_number = 1
    for (line in split(source, "\n")) {
        for (token in tokens) {
            if (contains(line, token)) {
                findings = findings ++ [{path: path, line: line_number, token: token}]
            }
        }
        line_number = line_number + 1
    }
    return findings
}

fn forbidden_symbol(name: string) bool =>
    starts_with(name, "_js_") or starts_with(name, "_heap_") or
    contains(name, "node_events_") or contains(name, "node_url_")

pub pn object_violations(symbols) {
    var forbidden = []
    for (name in symbols) {
        if (forbidden_symbol(name)) { forbidden = forbidden ++ [name] }
    }
    return sort(forbidden)
}

// Match Python's sorted source_files() before reading or scanning contents.
pub pn scan_tree() {
    var paths = []
    for (path in \.lambda.module.**) {
        if (path.is_file and source_file(path.name)) {
            paths = paths ++ [relative_path(path)]
        }
    }
    paths = sort(paths)
    var modules = []
    var violations = []
    for (name in node_modules) {
        let root = "lambda/module/" ++ name
        var count = 0
        for (path in paths) {
            if (starts_with(path, root ++ "/")) {
                count = count + 1
                violations = violations ++ scan_source(path, input(path, "text")^)
            }
        }
        modules = modules ++ [{name: name, present: exists(root), source_count: count}]
    }
    return {schema_version: 1, modules: modules, violations: violations}
}

pub pn check_node_module_link_inputs() {
    let config = input("build_lambda_config.json", "json")^
    let forbidden = ["zlib", "uv", "curl", "mbedtls", "mbedcrypto",
                     "mbedx509", "openssl", "ssl", "crypto"]
    var findings = []
    for (module in config.node_modules) {
        let libraries = if (module.libraries != null) module.libraries else []
        for (name in libraries) {
            if (contains(forbidden, lower(name))) {
                findings = findings ++ ["node target " ++ module.name ++
                                        " links host-owned dependency: " ++ name]
            }
        }
    }
    return findings
}
