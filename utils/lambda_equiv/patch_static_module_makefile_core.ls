// Native Lambda port of utils/patch_static_module_makefile.py.
pn platform_libraries(target) {
    let original = if (target.libraries != null) target.libraries else []
    let override = target.macos
    let added = if (override != null and override.additional_libraries != null)
                override.additional_libraries else []
    let excluded = if (override != null and override.exclude_libraries != null)
                   override.exclude_libraries else []
    var extended = original
    for (name in added) {
        if (not contains(extended, name)) { extended = extended ++ [name] }
    }
    var libraries = []
    for (name in extended) {
        if (not contains(excluded, name)) { libraries = libraries ++ [name] }
    }
    return libraries
}

pn visit_target(target, targets, external, root,
                var archives, var visited_targets, var visited_external) {
    let name = target.name
    if (contains(visited_targets, name)) { return }
    visited_targets = visited_targets ++ [name]
    for (dependency in platform_libraries(target)) {
        let library = external[dependency]
        if (library != null) {
            if (not contains(visited_external, dependency) and library.link == "static") {
                visited_external = visited_external ++ [dependency]
                let archive = library.lib
                if (archive != null and archive != "" and not starts_with(archive, "-")) {
                    archives = archives ++ [if (starts_with(archive, "/")) archive else root ++ "/" ++ archive]
                }
            }
        }
        if (library == null) {
            let child = targets[dependency]
            if (child != null) {
                let ignored = visit_target(child, targets, external, root,
                                           archives, visited_targets, visited_external)
            }
        }
    }
    return
}

pn replace_first(content: string, marker: string, replacement: string) {
    let index = index_of(content, marker)
    if (index == null) { raise error("lambda-exe Makefile link command changed; cannot append static archives") }
    return slice(content, 0, index) ++ replacement ++ slice(content, index + len(marker))
}

pub pn patch_makefile(config_path: string, makefile_path: string, root: string) {
    let config = input(config_path, "json")^
    var targets = {}
    for (target in config.targets) {
        if (target.name != null) { targets[target.name] = target }
    }
    let executable = targets["lambda-exe"]
    if (executable == null) { return }

    var external = {}
    for (library in config.libraries) {
        if (library.name != null) { external[library.name] = library }
    }
    for (library in config.platforms.macos.libraries) {
        if (library.name != null) { external[library.name] = library }
    }

    var archives = []
    var visited_targets = []
    var visited_external = []
    visit_target(executable, targets, external, root,
                 archives, visited_targets, visited_external)

    let content = input(makefile_path, "text")^
    let linked = replace_first(content, "$(ALL_LDFLAGS) $(LIBS)",
                               "$(ALL_LDFLAGS) $(LIBS) $(LAMBDA_STATIC_LATE_ARCHIVES)")
    let line = "LAMBDA_STATIC_LATE_ARCHIVES += " ++ join(archives, " ") ++ "\n"
    let patched = replace_first(linked, "LINKCMD = ", line ++ "LINKCMD = ")
    let written = output(patched, makefile_path, "text")^
    return written
}
