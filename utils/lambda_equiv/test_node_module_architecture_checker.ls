// Native Lambda port of utils/test_node_module_architecture_checker.py.
import .check_node_module_architecture_core

pn expect_token(path: string, source: string, token: string) {
    for (finding in scan_source(path, source)) {
        if (finding.token == token) { return }
    }
    raise error("NODE_MODULE_ARCH_SELFTEST: checker accepted forbidden token " ++ token)
}

pn main() {
    let core = "lambda/module/node_core/probe.cpp"
    let fs = "lambda/module/node_fs/node_fs_module.cpp"
    expect_token(core, "#include \"../../js/js_runtime.h\"", "\"../../js/")
    expect_token(core, "#include \"../../lambda.hpp\"", "\"../../lambda.hpp\"")
    expect_token(core, "uv_tcp_init(loop, &socket);", "uv_")
    expect_token(core, "heap_register_gc_root(&item.item);", "heap_register_gc_root")
    expect_token(fs, "#include <unistd.h>\n", "<unistd.h>")
    expect_token(fs, "NODE_FS_OPEN(path, flags, mode);\n", "NODE_FS_OPEN(")

    let violations = object_violations(["_file_getcwd", "_js_property_get", "_heap_create_name"])
    if (violations != ["_heap_create_name", "_js_property_get"]) {
        raise error("NODE_MODULE_ARCH_SELFTEST: symbol boundary mismatch")
    }
    if (len(scan_source("lambda/module/node_core/clean.cpp",
                        "#include \"../../jube/jube.h\"\n")) != 0) {
        raise error("NODE_MODULE_ARCH_SELFTEST: checker rejected a Jube-only source")
    }
    if (object_violations(["_jube_module", "_js_property_get"]) != ["_js_property_get"]) {
        raise error("NODE_MODULE_ARCH_SELFTEST: Jube symbol boundary mismatch")
    }
    if (object_violations(["__Z13node_url_initPK11JubeHostAPI"]) !=
        ["__Z13node_url_initPK11JubeHostAPI"]) {
        raise error("NODE_MODULE_ARCH_SELFTEST: shared Node primitive escaped")
    }
    print("NODE_MODULE_ARCH_SELFTEST: passed\n")
    return
}
