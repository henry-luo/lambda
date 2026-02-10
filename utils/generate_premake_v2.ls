// generate_premake_v2.ls - Lambda Script Premake5 Generator
// Usage: PLATFORM=macos ./lambda.exe run utils/generate_premake_v2.ls
// Generates Premake5 Lua build file from build_lambda_config.json
//
// Architecture: All array/data computation happens procedurally in pn functions
// to work around comprehension-based array building memory issues.
// fn functions do string formatting only.
//
// Source enumeration: Uses input(dir, "dir") to enumerate source files directly.
// Cross-platform: Accepts platform argument to generate for macOS, Linux, or Windows.

// ============================================================
// === Core String Utilities ===
// ============================================================

fn q(s) { "\""; s; "\"" }

fn ind(n) {
    if (n <= 0) ""
    else if (n == 1) "    "
    else if (n == 2) "        "
    else if (n == 3) "            "
    else "                "
}

fn strip_exe(name) {
    if (ends_with(name, ".exe")) slice(name, 0, len(name) - 4) else name
}

fn detect_platform() {
    let p = sys.os.platform;
    if (p == "darwin") "macos" else if (p == "linux") "linux" else "windows"
}

// ============================================================
// === Library Lookup (pure fn - no array building) ===
// ============================================================

fn find_in_libs(libs, name) {
    let found = [for (lib in libs where lib.name == name) lib];
    if (len(found) > 0) found[0] else null
}

fn find_target(targets, name) {
    let found = [for (t in targets where t.name == name) t];
    if (len(found) > 0) found[0] else null
}

// ============================================================
// === Lua Block Generators (fn - string building only) ===
// ============================================================

// multi-line block with trailing comma on each item (comments starting with -- are unquoted)
fn lua_block(name, items, depth) {
    let formatted = [for (item in items)
        if (starts_with(item, "--")) ind(depth + 1) ++ item
        else ind(depth + 1) ++ q(item) ++ ","];
    ind(depth); name; " {\n"; str_join(formatted, "\n"); "\n"; ind(depth); "}\n"
}

// multi-line defines block with trailing comma
fn gen_defines_block(defs, depth) {
    if (len(defs) == 0) ""
    else ind(depth) ++ "defines {\n" ++ str_join([for (d in defs) ind(depth + 1) ++ q(d) ++ ","], "\n") ++ "\n" ++ ind(depth) ++ "}\n"
}

fn gen_files_block(files, depth) {
    if (len(files) == 0) "" else lua_block("files", files, depth)
}

fn gen_includes_block(includes, depth) {
    if (len(includes) == 0) "" else lua_block("includedirs", includes, depth)
}

// Generate links block with blank line separator before late-binding entries
fn gen_links_block_impl(links, depth) {
    // Filter late-binding entries (starting with ":") using pipe where
    let late_items = links where starts_with(~, ":");
    let normal_items = links where not starts_with(~, ":");
    if (late_items != null and normal_items != null) {
        // Both normal and late entries: separate with blank line
        let normal_lines = [for (l in normal_items) ind(depth + 1) ++ q(l) ++ ","];
        let late_lines = [for (l in late_items) ind(depth + 1) ++ q(l) ++ ","];
        ind(depth); "links {\n";
        str_join(normal_lines, "\n"); "\n";
        ind(depth); "\n";
        str_join(late_lines, "\n"); "\n";
        ind(depth); "}\n"
    } else if (late_items != null) {
        // Only late entries: blank line before them
        let late_lines = [for (l in late_items) ind(depth + 1) ++ q(l) ++ ","];
        ind(depth); "links {\n";
        ind(depth); "\n";
        str_join(late_lines, "\n"); "\n";
        ind(depth); "}\n"
    } else {
        lua_block("links", links, depth)
    }
}

fn gen_links_block(links, depth) {
    if (len(links) == 0) ""
    else gen_links_block_impl(links, depth)
}

fn gen_empty_links_block(depth) {
    ind(depth); "links {\n"; ind(depth); "}\n"
}

fn gen_buildopts_block(opts, depth) {
    if (len(opts) == 0) "" else lua_block("buildoptions", opts, depth)
}

fn gen_linkopts_block(paths, depth) {
    if (len(paths) == 0) "" else lua_block("linkoptions", paths, depth)
}

fn gen_empty_linkopts_block(depth) {
    ind(depth); "linkoptions {\n"; ind(depth); "}\n"
}

fn gen_libdirs_block(dirs, depth) {
    if (len(dirs) == 0) "" else lua_block("libdirs", dirs, depth)
}

fn gen_removefiles_block(patterns, depth) {
    if (len(patterns) == 0) "" else lua_block("removefiles", patterns, depth)
}

// ============================================================
// === Platform-Specific Helpers ===
// ============================================================

fn get_libdirs(platform) {
    if (platform == "windows") {
        ["/mingw64/lib", "win-native-deps/lib", "build/lib"]
    } else {
        ["/opt/homebrew/lib", "/usr/local/lib", "build/lib"]
    }
}

fn get_lib_libdirs(platform) {
    if (platform == "windows") {
        ["/mingw64/lib", "win-native-deps/lib"]
    } else {
        ["/opt/homebrew/lib", "/usr/local/lib"]
    }
}

fn get_test_libdirs(platform) {
    if (platform == "windows") {
        ["/mingw64/lib", "win-native-deps/lib", "build/lib"]
    } else if (platform == "linux") {
        ["/usr/local/lib", "/usr/local/lib/aarch64-linux-gnu", "/usr/lib/aarch64-linux-gnu", "build/lib"]
    } else {
        ["/opt/homebrew/lib", "/opt/homebrew/Cellar/criterion/2.4.2_2/lib", "/usr/local/lib", "build/lib"]
    }
}

fn get_cpp_stdlib(platform) {
    if (platform == "macos") "c++"
    else if (platform == "linux") "stdc++"
    else ""
}

// ============================================================
// === Section Generators ===
// ============================================================

fn gen_header(platform) {
    let pname = if (platform == "macos") "macOS"
                else if (platform == "linux") "Linux" else "Windows";
    "-- Generated by utils/generate_premake.py for "; pname; "\n";
    "-- Lambda Build System Premake5 Configuration\n";
    "-- Platform: "; pname; "\n";
    "-- DO NOT EDIT MANUALLY - Regenerate using: python3 utils/generate_premake.py\n";
    "\n"
}

fn gen_workspace(c) {
    let out = c.output or "lambda.exe";
    let start = strip_exe(out);
    "workspace \"Lambda\"\n";
    ind(1); "configurations { \"Debug\", \"Release\" }\n";
    ind(1); "platforms { \"native\" }\n";
    ind(1); "location \"build/premake\"\n";
    ind(1); "startproject "; q(start); "\n";
    ind(1); "toolset \"gcc\"\n";
    ind(1); "\n";
    ind(1); "-- Global settings\n";
    ind(1); "cppdialect \"C++17\"\n";
    ind(1); "cdialect \"C99\"\n";
    ind(1); "warnings \"Extra\"\n";
    ind(1); "\n"
}

pn gen_debug_config(platform, config) {
    var base = ind(1) ++ "filter \"configurations:Debug\"\n"
        ++ ind(2) ++ "defines { \"DEBUG\" }\n"
        ++ ind(2) ++ "symbols \"On\"\n"
        ++ ind(2) ++ "optimize \"Off\"\n"
    if (platform == "windows") {
        var plats = config.platforms or {}
        var win = plats.windows or {}
        var flags = win.linker_flags or []
        if (len(flags) > 0) {
            var items = []
            var i = 0
            while (i < len(flags)) {
                items = arr_push(items, ind(3) ++ q("-" ++ flags[i]) ++ ",")
                i = i + 1
            }
            return base ++ ind(2) ++ "linkoptions {\n"
                ++ str_join(items, "\n")
                ++ "\n" ++ ind(2) ++ "}\n"
                ++ ind(1) ++ "\n"
        }
    }
    return base ++ ind(1) ++ "\n"
}

fn gen_release_config_macos() {
    ind(1); "filter \"configurations:Release\"\n";
    ind(2); "defines { \"NDEBUG\" }\n";
    ind(2); "symbols \"Off\"\n";
    ind(2); "optimize \"On\"\n";
    ind(2); "-- Dead code elimination and ThinLTO\n";
    ind(2); "buildoptions {\n";
    ind(3); "\"-flto=thin\",\n";
    ind(3); "\"-ffunction-sections\",\n";
    ind(3); "\"-fdata-sections\",\n";
    ind(3); "\"-fvisibility=hidden\",\n";
    ind(3); "\"-fvisibility-inlines-hidden\",\n";
    ind(2); "}\n";
    ind(2); "-- macOS: strip dead code and symbols with ThinLTO\n";
    ind(2); "linkoptions {\n";
    ind(3); "\"-flto=thin\",\n";
    ind(3); "\"-Wl,-dead_strip\",\n";
    ind(3); "\"-Wl,-x\",  -- Strip local symbols\n";
    ind(2); "}\n";
    ind(1); "\n"
}

fn gen_release_config_linux() {
    ind(1); "filter \"configurations:Release\"\n";
    ind(2); "defines { \"NDEBUG\" }\n";
    ind(2); "symbols \"Off\"\n";
    ind(2); "optimize \"On\"\n";
    ind(2); "-- Dead code elimination and ThinLTO\n";
    ind(2); "buildoptions {\n";
    ind(3); "\"-flto=thin\",\n";
    ind(3); "\"-ffunction-sections\",\n";
    ind(3); "\"-fdata-sections\",\n";
    ind(3); "\"-fvisibility=hidden\",\n";
    ind(3); "\"-fvisibility-inlines-hidden\",\n";
    ind(2); "}\n";
    ind(2); "-- Linux: strip dead code and symbols with ThinLTO\n";
    ind(2); "linkoptions {\n";
    ind(3); "\"-flto=thin\",\n";
    ind(3); "\"-Wl,--gc-sections\",\n";
    ind(3); "\"-Wl,--strip-all\",\n";
    ind(2); "}\n";
    ind(1); "\n"
}

fn gen_release_config_windows() {
    ind(1); "filter \"configurations:Release\"\n";
    ind(2); "defines { \"NDEBUG\" }\n";
    ind(2); "symbols \"Off\"\n";
    ind(2); "optimize \"On\"\n";
    ind(2); "-- Dead code elimination and ThinLTO\n";
    ind(2); "buildoptions {\n";
    ind(3); "\"-flto=thin\",\n";
    ind(3); "\"-ffunction-sections\",\n";
    ind(3); "\"-fdata-sections\",\n";
    ind(3); "\"-fvisibility=hidden\",\n";
    ind(3); "\"-fvisibility-inlines-hidden\",\n";
    ind(2); "}\n";
    ind(2); "-- Windows: strip dead code with ThinLTO\n";
    ind(2); "linkoptions {\n";
    ind(3); "\"-flto=thin\",\n";
    ind(3); "\"-Wl,--gc-sections\",\n";
    ind(3); "\"-s\",  -- Strip symbols\n";
    ind(2); "}\n";
    ind(1); "\n"
}

fn gen_platform_globals() {
    ind(1); "-- Native Linux build settings\n";
    ind(1); "toolset \"gcc\"\n";
    ind(1); "defines { \"LINUX\", \"_GNU_SOURCE\", \"NATIVE_LINUX_BUILD\" }\n";
    ind(1); "\n"; ind(1); "\n";
    ind(1); "filter {}\n";
    "\n"
}

// === Project Header (no blank line between objdir and targetname) ===

fn gen_project_header(name, kind_str, lang, tdir) {
    "project "; q(name); "\n";
    ind(1); "kind "; q(kind_str); "\n";
    ind(1); "language "; q(lang); "\n";
    ind(1); "targetdir "; q(tdir); "\n";
    ind(1); "objdir \"build/obj/%{prj.name}\"\n"
}

// === Filter Blocks (for mixed C/C++ projects) ===

fn gen_filter_blocks(build_opts) {
    let c_opts = build_opts ++ ["-std=c17"];
    let cpp_opts = build_opts ++ ["-std=c++17"];
    ind(1); "filter \"files:**.c\"\n";
    ind(2); "buildoptions {\n";
    str_join([for (opt in c_opts) ind(3) ++ q(opt) ++ ","], "\n"); "\n";
    ind(2); "}\n";
    ind(1); "\n";
    ind(1); "filter \"files:**.cpp\"\n";
    ind(2); "buildoptions {\n";
    str_join([for (opt in cpp_opts) ind(3) ++ q(opt) ++ ","], "\n"); "\n";
    ind(2); "}\n";
    ind(1); "\n";
    ind(1); "filter {}\n"
}

// === Frameworks ===

fn gen_frameworks_linkopts(depth) {
    ind(depth); "linkoptions {\n";
    ind(depth + 1); "\"-framework CoreFoundation\",\n";
    ind(depth + 1); "\"-framework CoreServices\",\n";
    ind(depth + 1); "\"-framework SystemConfiguration\",\n";
    ind(depth + 1); "\"-framework Cocoa\",\n";
    ind(depth + 1); "\"-framework IOKit\",\n";
    ind(depth + 1); "\"-framework CoreVideo\",\n";
    ind(depth + 1); "\"-framework OpenGL\",\n";
    ind(depth + 1); "\"-framework Foundation\",\n";
    ind(depth + 1); "\"-framework CoreGraphics\",\n";
    ind(depth + 1); "\"-framework AppKit\",\n";
    ind(depth + 1); "\"-framework Carbon\",\n";
    ind(depth); "}\n"
}

// ============================================================
// === lambda-lib Project ===
// ============================================================

fn gen_lambda_lib(t, lib_includes, platform, build_opts) {
    let sources = t.sources or [];
    let lib_names = t.libraries or [];
    let defines = t.defines or [];
    let libdirs = get_lib_libdirs(platform);
    gen_project_header("lambda-lib", "StaticLib", "C", "build/lib");
    ind(1); "\n";
    ind(1); "-- Meta-library: combines source files from dependencies\n";
    gen_files_block(sources, 1);
    ind(1); "\n";
    gen_includes_block(lib_includes, 1);
    ind(1); "\n";
    gen_libdirs_block(libdirs, 1);
    ind(1); "\n";
    gen_links_block(lib_names, 1);
    ind(1); "\n";
    gen_buildopts_block(build_opts, 1);
    ind(1); "\n";
    gen_defines_block(defines, 1);
    ind(1); "\n";
    "\n"
}

// ============================================================
// === lambda-input-full-cpp Project ===
// ============================================================

fn gen_input_full(t, input_includes, static_paths, dyn_links, platform, build_opts) {
    let source_files = t.source_files or [];
    let patterns = t.source_patterns or [];
    let excludes = t.exclude_patterns or [];
    let defines = t.defines or [];
    let link_type = t.link or "static";
    let kind_str = if (link_type == "dynamic") "SharedLib" else "StaticLib";
    let libdirs = get_libdirs(platform);
    let cpp_stdlib = get_cpp_stdlib(platform);
    gen_project_header("lambda-input-full-cpp", kind_str, "C++", "build/lib");
    ind(1); "\n";
    // files block - individual source files
    gen_files_block(source_files, 1);
    ind(1); "\n";
    // pattern files blocks - each pattern in its own files block
    str_join([for (p in patterns)
        ind(1) ++ "files {\n" ++ ind(2) ++ q(p) ++ ",\n" ++ ind(1) ++ "}\n" ++ ind(1)], "\n");
    "\n";
    // removefiles
    if (len(excludes) > 0) gen_removefiles_block(excludes, 1) ++ ind(1) ++ "\n" else "";
    // includes
    gen_includes_block(input_includes, 1);
    ind(1); "\n";
    // filter blocks for mixed C/C++
    gen_filter_blocks(build_opts);
    ind(1); "\n";
    // libdirs
    gen_libdirs_block(libdirs, 1);
    ind(1); "\n";
    // static linkoptions (includes Windows system libs if present)
    gen_linkopts_block(static_paths, 1);
    ind(1); "\n";
    // dynamic links
    gen_links_block(dyn_links, 1);
    ind(1); "\n";
    // Windows-specific DLL export linkoptions
    if (platform == "windows")
        ind(1) ++ "linkoptions {\n"
            ++ ind(2) ++ "\"-Wl,--output-def,lambda-input-full-cpp.def\",\n"
            ++ ind(2) ++ "\"../../lambda-input-full-cpp.def\",\n"
            ++ ind(1) ++ "}\n"
            ++ ind(1) ++ "\n"
    else "";
    // defines
    gen_defines_block(defines, 1);
    ind(1); "\n";
    // frameworks (macOS)
    if (platform == "macos") ind(1) ++ "-- Add macOS frameworks\n" ++ gen_frameworks_linkopts(1) ++ ind(1) ++ "\n" else "";
    // c++ stdlib
    if (cpp_stdlib != "") {
        ind(1); "-- Automatically added C++ standard library\n";
        gen_links_block([cpp_stdlib], 1);
        ind(1); "\n"
    } else { "" }
    "\n"
}

// ============================================================
// === lambda-input-full Wrapper ===
// ============================================================

fn gen_input_wrapper() {
    gen_project_header("lambda-input-full", "SharedLib", "C++", "build/lib");
    ind(1); "\n";
    ind(1); "-- Wrapper library with empty source file\n";
    ind(1); "files {\n";
    ind(2); "\"utils/empty.cpp\",\n";
    ind(1); "}\n";
    ind(1); "\n";
    ind(1); "links {\n";
    ind(2); "\"lambda-input-full-cpp\",\n";
    ind(1); "}\n";
    ind(1); "\n";
    "\n"
}

// ============================================================
// === Main Executable ===
// ============================================================

fn gen_exe_links_macos() {
    ind(1); "-- Dynamic libraries\n";
    ind(1); "filter \"platforms:native\"\n";
    ind(2); "links {\n";
    ind(3); "\"re2\",\n";
    ind(2); "}\n";
    ind(1); "\n";
    gen_frameworks_linkopts(2);
    ind(1); "\n";
    ind(1); "filter {}\n";
    ind(1); "\n"
}

fn gen_exe_links_linux(dyn_libs) {
    ind(1); "-- Dynamic libraries\n";
    ind(1); "filter \"platforms:native\"\n";
    ind(2); "links {\n";
    str_join([for (lib in dyn_libs) ind(3) ++ q(lib) ++ ","], "\n"); "\n";
    ind(2); "}\n";
    ind(1); "\n";
    ind(1); "filter {}\n";
    ind(1); "\n"
}

fn gen_exe_links_windows(dyn_libs) {
    ind(1); "-- Dynamic libraries\n";
    ind(1); "filter \"platforms:native\"\n";
    ind(2); "links {\n";
    str_join([for (lib in dyn_libs) ind(3) ++ q(lib) ++ ","], "\n"); "\n";
    ind(2); "}\n";
    ind(1); "\n";
    ind(1); "filter {}\n";
    ind(1); "\n"
}

fn gen_lambda_exe(name, files, full_includes, static_paths, platform, build_opts, dyn_libs, extra_defines) {
    let libdirs = get_libdirs(platform);
    gen_project_header(name, "ConsoleApp", "C++", ".");
    ind(1); "targetname "; q(name); "\n";
    ind(1); "targetextension \".exe\"\n";
    ind(1); "\n";
    gen_files_block(files, 1);
    ind(1); "\n";
    gen_includes_block(full_includes, 1);
    ind(1); "\n";
    gen_libdirs_block(libdirs, 1);
    ind(1); "\n";
    gen_linkopts_block(static_paths, 1);
    ind(1); "\n";
    // dynamic libraries
    if (platform == "macos") gen_exe_links_macos()
    else if (platform == "linux") gen_exe_links_linux(dyn_libs)
    else gen_exe_links_windows(dyn_libs);
    gen_buildopts_block(build_opts, 1);
    ind(1); "\n";
    ind(1); "-- C++ specific options\n";
    ind(1); "filter \"files:**.cpp\"\n";
    ind(2); "buildoptions { \"-std=c++17\" }\n";
    ind(1); "\n";
    ind(1); "-- C specific options\n";
    ind(1); "filter \"files:**.c\"\n";
    ind(2); "buildoptions { \"-std=c99\" }\n";
    ind(1); "\n";
    ind(1); "filter {}\n";
    ind(1); "\n";
    // defines
    if (len(extra_defines) > 0) {
        let all_defs = ["_GNU_SOURCE"] ++ extra_defines;
        gen_defines_block(all_defs, 1)
    } else {
        gen_defines_block(["_GNU_SOURCE"], 1)
    }
    ind(1); "\n";
    "\n"
}

// ============================================================
// === Test Project - Simple (no lambda-input-full) ===
// ============================================================

fn gen_test_simple(proj_name, sources, full_includes, link_names, lib_paths, defs, all_opts, platform, disable_asan) {
    let test_libdirs = get_test_libdirs(platform);
    gen_project_header(proj_name, "ConsoleApp", "C++", "test");
    ind(1); "targetname "; q(proj_name); "\n";
    ind(1); "targetextension \".exe\"\n";
    ind(1); "\n";
    gen_files_block(sources, 1);
    ind(1); "\n";
    gen_includes_block(full_includes, 1);
    ind(1); "\n";
    if (len(defs) > 0) gen_defines_block(defs, 1) ++ ind(1) ++ "\n" else "";
    gen_libdirs_block(test_libdirs, 1);
    ind(1); "\n";
    if (len(link_names) > 0) gen_links_block(link_names, 1) else gen_empty_links_block(1);
    ind(1); "\n";
    gen_linkopts_block(lib_paths, 1);
    ind(1); "\n";
    // build options
    gen_buildopts_block(all_opts, 1);
    ind(1); "\n";
    // ASAN
    if (not disable_asan) {
        ind(1); "-- AddressSanitizer for test projects only\n";
        ind(1); "filter { \"configurations:Debug\", \"not platforms:Linux_x64\" }\n";
        ind(2); "buildoptions { \"-fsanitize=address\", \"-fno-omit-frame-pointer\" }\n";
        ind(2); "linkoptions { \"-fsanitize=address\" }\n";
        ind(1); "\n";
        ind(1); "filter {}\n";
        ind(1); "\n"
    } else { "" }
    "\n"
}

// ============================================================
// === Test Project - With lambda-input-full dependency ===
// ============================================================

fn gen_test_input_full(proj_name, sources, full_includes, link_names, lib_paths, defs, all_opts, platform, disable_asan, input_full_linkopts, input_full_dyn_links, force_load_opts) {
    let test_libdirs = get_test_libdirs(platform);
    gen_project_header(proj_name, "ConsoleApp", "C++", "test");
    ind(1); "targetname "; q(proj_name); "\n";
    ind(1); "targetextension \".exe\"\n";
    ind(1); "\n";
    gen_files_block(sources, 1);
    ind(1); "\n";
    gen_includes_block(full_includes, 1);
    ind(1); "\n";
    if (len(defs) > 0) gen_defines_block(defs, 1) ++ ind(1) ++ "\n" else "";
    gen_libdirs_block(test_libdirs, 1);
    ind(1); "\n";
    gen_links_block(link_names, 1);
    ind(1); "\n";
    gen_linkopts_block(lib_paths, 1);
    ind(1); "\n";
    // input-full linkoptions (link-groups on Linux, allow-multiple-def on Windows, empty on macOS)
    if (len(input_full_linkopts) > 0) gen_linkopts_block(input_full_linkopts, 1) else gen_empty_linkopts_block(1);
    ind(1); "\n";
    // dynamic libraries
    ind(1); "-- Add dynamic libraries\n";
    if (len(input_full_dyn_links) > 0) lua_block("links", input_full_dyn_links, 1) else gen_empty_links_block(1);
    ind(1); "\n";
    // tree-sitter linkoptions (empty)
    ind(1); "-- Add tree-sitter libraries using linkoptions to append to LIBS section\n";
    gen_empty_linkopts_block(1);
    ind(1); "\n";
    // frameworks (macOS: populated, others: empty)
    if (platform == "macos") {
        ind(1); "-- Add macOS frameworks\n";
        gen_frameworks_linkopts(1)
    } else {
        ind(1); "-- Add macOS frameworks\n";
        gen_empty_linkopts_block(1)
    }
    ind(1); "\n";
    // build options
    gen_buildopts_block(all_opts, 1);
    ind(1); "\n";
    // force_load / whole-archive tree-sitter libs
    ind(1); "filter {}\n";
    gen_linkopts_block(force_load_opts, 1);
    ind(1); "\n";
    // ASAN
    if (not disable_asan) {
        ind(1); "-- AddressSanitizer for test projects only\n";
        ind(1); "filter { \"configurations:Debug\", \"not platforms:Linux_x64\" }\n";
        ind(2); "buildoptions { \"-fsanitize=address\", \"-fno-omit-frame-pointer\" }\n";
        ind(2); "linkoptions { \"-fsanitize=address\" }\n";
        ind(1); "\n";
        ind(1); "filter {}\n";
        ind(1); "\n"
    } else { "" }
    "\n"
}


// ============================================================
// === Procedural Data Computation ===
// ============================================================

// array push - append item to array using ++ operator
pn arr_push(arr, item) {
    var result = arr ++ [item]
    return result
}

// prefix relative path with ../../ for premake build directory
pn make_build_path(p) {
    if (len(p) == 0) { return p }
    if (starts_with(p, "/")) { return p }
    if (starts_with(p, "-Wl,")) { return p }
    if (starts_with(p, "-l")) { return p }
    return "../../" ++ p
}

// check if item is in array
pn arr_contains(arr, item) {
    var i = 0
    while (i < len(arr)) {
        if (arr[i] == item) { return true }
        i = i + 1
    }
    return false
}

// add item to array only if not already present
pn arr_push_unique(arr, item) {
    if (item == null or item == "") { return arr }
    if (arr_contains(arr, item)) { return arr }
    return arr_push(arr, item)
}

// ============================================================
// === Build merged external_libraries map ===
// ============================================================

// Builds a flat array of {name, include, lib, link} entries
// merging global libraries + dev_libraries + platform overrides
pn build_external_libs(config, platform) {
    var result = []

    // step 1: global libraries
    var libs = config.libraries or []
    var i = 0
    while (i < len(libs)) {
        var lib = libs[i]
        result = arr_push(result, {
            name: lib.name,
            include: lib.include or "",
            lib: lib.lib or "",
            link: lib.link or "static"
        })
        i = i + 1
    }

    // step 2: dev_libraries
    var dev_libs = config.dev_libraries or []
    i = 0
    while (i < len(dev_libs)) {
        var lib = dev_libs[i]
        result = arr_push(result, {
            name: lib.name,
            include: lib.include or "",
            lib: lib.lib or "",
            link: lib.link or "static"
        })
        i = i + 1
    }

    // step 3: platform-specific overrides
    var plats = config.platforms or {}
    var plat = if (platform == "macos") plats.macos or {}
               else if (platform == "linux") plats.linux or {}
               else plats.windows or {}
    var plat_libs = plat.libraries or []
    i = 0
    while (i < len(plat_libs)) {
        var lib = plat_libs[i]
        var name = lib.name
        var link_val = lib.link or (if (platform == "macos") "dynamic" else "static")
        // check if already exists - override it
        var found = false
        var j = 0
        while (j < len(result)) {
            if (result[j].name == name) {
                found = true
                break
            }
            j = j + 1
        }
        if (not found) {
            result = arr_push(result, {
                name: name,
                include: lib.include or "",
                lib: lib.lib or "",
                link: link_val
            })
        } else {
            var new_result = []
            j = 0
            while (j < len(result)) {
                if (result[j].name == name) {
                    new_result = arr_push(new_result, {
                        name: name,
                        include: lib.include or "",
                        lib: lib.lib or "",
                        link: link_val
                    })
                } else {
                    new_result = arr_push(new_result, result[j])
                }
                j = j + 1
            }
            result = new_result
        }
        i = i + 1
    }

    // platform dev_libraries (override existing entries, like gtest)
    var plat_dev = plat.dev_libraries or []
    i = 0
    while (i < len(plat_dev)) {
        var lib = plat_dev[i]
        var name = lib.name
        var link_val = lib.link or "static"
        // check if already exists - override it
        var found = false
        var j = 0
        while (j < len(result)) {
            if (result[j].name == name) {
                found = true
                break
            }
            j = j + 1
        }
        if (not found) {
            result = arr_push(result, {
                name: name,
                include: lib.include or "",
                lib: lib.lib or "",
                link: link_val
            })
        } else {
            var new_result = []
            j = 0
            while (j < len(result)) {
                if (result[j].name == name) {
                    new_result = arr_push(new_result, {
                        name: name,
                        include: lib.include or "",
                        lib: lib.lib or "",
                        link: link_val
                    })
                } else {
                    new_result = arr_push(new_result, result[j])
                }
                j = j + 1
            }
            result = new_result
        }
        i = i + 1
    }

    return result
}

// find in external libs array
pn find_ext_lib(ext_libs, name) {
    var i = 0
    while (i < len(ext_libs)) {
        if (ext_libs[i].name == name) { return ext_libs[i] }
        i = i + 1
    }
    return null
}

// ============================================================
// === Build Include Lists ===
// ============================================================

// Consolidated includes: global + platform (no library includes)
pn build_consolidated_includes(config, platform) {
    var result = []
    var global_inc = config.includes or []
    var i = 0
    while (i < len(global_inc)) {
        result = arr_push_unique(result, global_inc[i])
        i = i + 1
    }
    var plats = config.platforms or {}
    var plat = if (platform == "macos") plats.macos or {}
               else if (platform == "linux") plats.linux or {}
               else plats.windows or {}
    var plat_inc = plat.includes or []
    i = 0
    while (i < len(plat_inc)) {
        result = arr_push_unique(result, plat_inc[i])
        i = i + 1
    }
    return result
}

// lambda-lib includes: consolidated + lib/mem-pool/include + dependency includes
pn build_lib_includes(config, platform, ext_libs) {
    var result = build_consolidated_includes(config, platform)
    result = arr_push_unique(result, "lib/mem-pool/include")
    // add includes for lambda-lib dependencies
    var targets = config.targets or []
    var t = find_target(targets, "lambda-lib")
    if (t != null) {
        var deps = t.libraries or []
        var i = 0
        while (i < len(deps)) {
            var lib = find_ext_lib(ext_libs, deps[i])
            if (lib != null and lib.include != null and lib.include != "") {
                result = arr_push_unique(result, lib.include)
            }
            i = i + 1
        }
    }
    return result
}

// input-full-cpp includes: consolidated + dependency includes (NO lib/mem-pool/include)
pn build_input_includes(config, platform, ext_libs) {
    var result = build_consolidated_includes(config, platform)
    // add includes for input-full dependencies
    var targets = config.targets or []
    var t = find_target(targets, "lambda-input-full")
    if (t != null) {
        var deps = t.libraries or []
        var i = 0
        while (i < len(deps)) {
            var lib = find_ext_lib(ext_libs, deps[i])
            if (lib != null and lib.include != null and lib.include != "") {
                result = arr_push_unique(result, lib.include)
            }
            i = i + 1
        }
    }
    return result
}

// full includes: consolidated + lib/mem-pool/include + ALL external lib includes
// (for main exe and tests)
pn build_full_includes(config, platform, ext_libs) {
    var result = build_consolidated_includes(config, platform)
    result = arr_push_unique(result, "lib/mem-pool/include")
    // add ALL external library includes (except dev_libraries are included by Python too)
    var i = 0
    while (i < len(ext_libs)) {
        var lib = ext_libs[i]
        if (lib.link != "none" and lib.include != null and lib.include != "") {
            result = arr_push_unique(result, lib.include)
        }
        i = i + 1
    }
    return result
}

// ============================================================
// === Build Platform-Specific Options ===
// ============================================================

// Build compiler options array from config flags + platform flags
pn build_platform_build_opts(config, platform) {
    var result = []

    // start with global flags
    var global_flags = config.flags or []
    var i = 0
    while (i < len(global_flags)) {
        var flag = global_flags[i]
        var opt = if (starts_with(flag, "-")) flag else "-" ++ flag
        result = arr_push_unique(result, opt)
        i = i + 1
    }

    // add platform-specific flags
    var plats = config.platforms or {}
    var plat = if (platform == "macos") plats.macos or {}
               else if (platform == "linux") plats.linux or {}
               else plats.windows or {}
    var plat_flags = plat.flags or []
    i = 0
    while (i < len(plat_flags)) {
        var flag = plat_flags[i]
        var opt = if (starts_with(flag, "-")) flag else "-" ++ flag
        result = arr_push_unique(result, opt)
        i = i + 1
    }

    return result
}

// ============================================================
// === Build Static Library Paths ===
// ============================================================

// input-full-cpp static library paths
pn build_input_static_paths(config, ext_libs, platform) {
    var result = []
    var targets = config.targets or []
    var t = find_target(targets, "lambda-input-full")
    if (t == null) { return result }

    var lib_names = t.libraries or []
    var i = 0
    while (i < len(lib_names)) {
        var name = lib_names[i]
        var lib = find_ext_lib(ext_libs, name)
        if (lib != null and lib.link == "static") {
            // on Windows, skip late-binding libs (rpmalloc)
            if (platform == "windows" and name == "rpmalloc") {
                i = i + 1
                continue
            }
            var p = lib.lib or ""
            if (p != "") {
                result = arr_push(result, make_build_path(p))
            }
        }
        i = i + 1
    }

    // Windows system libs for static linking
    if (platform == "windows") {
        result = arr_push(result, "-lws2_32")
        result = arr_push(result, "-lwsock32")
        result = arr_push(result, "-lwinmm")
        result = arr_push(result, "-lcrypt32")
        result = arr_push(result, "-lbcrypt")
        result = arr_push(result, "-ladvapi32")
        // DLL export flags
        result = arr_push(result, "-Wl,--export-all-symbols")
        result = arr_push(result, "-Wl,--enable-auto-import")
    }

    return result
}

// input-full-cpp dynamic link names
pn build_input_dyn_links(config, ext_libs, platform) {
    var result = []
    var targets = config.targets or []
    var t = find_target(targets, "lambda-input-full")
    if (t == null) { return result }

    // first pass: collect dynamic external libraries
    var lib_names = t.libraries or []
    var i = 0
    while (i < len(lib_names)) {
        var name = lib_names[i]
        var lib = find_ext_lib(ext_libs, name)
        if (lib != null and lib.link == "dynamic") {
            // skip framework libs
            var lib_path = lib.lib or ""
            if (not starts_with(lib_path, "-framework ")) {
                if (starts_with(lib_path, "-l")) {
                    result = arr_push(result, slice(lib_path, 2, len(lib_path)))
                } else {
                    result = arr_push(result, lib.name)
                }
            }
        }
        i = i + 1
    }
    // second pass: add internal project links (lambda-lib) and libraries not in ext_libs
    // Python bug: libs with link="none" are removed from external_libraries dict, 
    // so they appear as "internal deps" and get added to links block
    i = 0
    while (i < len(lib_names)) {
        var name2 = lib_names[i]
        if (name2 == "lambda-lib") {
            result = arr_push(result, "lambda-lib")
        } else {
            var lib2 = find_ext_lib(ext_libs, name2)
            if (lib2 == null or lib2.link == "none") {
                // not in ext_libs — treated as internal dep (Python behavior)
                result = arr_push(result, name2)
            }
        }
        i = i + 1
    }

    // Windows: add late-binding libs
    if (platform == "windows") {
        // nghttp2 as "none" on windows, skip
        // rpmalloc as static -> late binding
        var rpmalloc = find_ext_lib(ext_libs, "rpmalloc")
        if (rpmalloc != null and rpmalloc.link == "static") {
            result = arr_push(result, "rpmalloc:static")
        }
    }

    return result
}

// Build platform-specific defines for input-full-cpp
pn build_input_defines(config, platform, target_defines) {
    var result = []

    if (platform == "windows") {
        // extract D-prefixed flags from Windows platform flags
        var plats = config.platforms or {}
        var win = plats.windows or {}
        var flags = win.flags or []
        var i = 0
        while (i < len(flags)) {
            var flag = flags[i]
            if (starts_with(flag, "D")) {
                result = arr_push(result, slice(flag, 1, len(flag)))
            }
            i = i + 1
        }
        result = arr_push(result, "UTF8PROC_STATIC")
        result = arr_push(result, "CURL_STATICLIB")
    }

    // add target-specific defines
    var i = 0
    while (i < len(target_defines)) {
        result = arr_push(result, target_defines[i])
        i = i + 1
    }

    return result
}

// Build exe dependencies order using Python's remove-and-append strategy
// Global lib names not in platform overrides (preserving global order),
// then platform override names (in their platform order)
pn build_exe_deps_order(config, platform) {
    var result = []
    var plats = config.platforms or {}
    var plat = if (platform == "macos") plats.macos or {}
               else if (platform == "linux") plats.linux or {}
               else plats.windows or {}
    var plat_libs = plat.libraries or []

    // Build set of platform override names
    var plat_names = []
    var i = 0
    while (i < len(plat_libs)) {
        plat_names = arr_push(plat_names, plat_libs[i].name)
        i = i + 1
    }

    // Step 1: global library names NOT in platform overrides (keep global order)
    var libs = config.libraries or []
    i = 0
    while (i < len(libs)) {
        var name = libs[i].name
        var in_plat = false
        var j = 0
        while (j < len(plat_names)) {
            if (plat_names[j] == name) {
                in_plat = true
                break
            }
            j = j + 1
        }
        if (not in_plat) {
            result = arr_push(result, name)
        }
        i = i + 1
    }

    // Step 2: platform override names in platform order
    i = 0
    while (i < len(plat_names)) {
        result = arr_push(result, plat_names[i])
        i = i + 1
    }

    return result
}

// Main exe static library paths (all global + platform static libs)
// Uses exe_deps_order for correct library ordering per platform
pn build_exe_static_paths(config, platform, ext_libs) {
    var result = []
    var exe_deps = build_exe_deps_order(config, platform)

    // First pass: iterate exe_deps in order, emit static lib paths
    // Skip: dynamic libs, dev_libraries (gtest), libedit (added later),
    //        libs with -l or -framework prefix, platform-only libs
    var skip_names = ["libedit", "gtest", "gtest_main"]
    var platform_only_names = ["pthread", "stdc++fs", "GL", "GLU", "gomp"]
    var i = 0
    while (i < len(exe_deps)) {
        var name = exe_deps[i]
        var lib = find_ext_lib(ext_libs, name)
        if (lib == null) {
            i = i + 1
            continue
        }
        // skip dynamic libs
        if (lib.link == "dynamic" or lib.link == "none") {
            i = i + 1
            continue
        }
        // skip libs handled later or dev libs
        var skip = false
        var j = 0
        while (j < len(skip_names)) {
            if (name == skip_names[j]) {
                skip = true
                break
            }
            j = j + 1
        }
        if (skip) {
            i = i + 1
            continue
        }
        // skip platform-only dynamic libs (static platform-only like gomp on windows are included)
        j = 0
        while (j < len(platform_only_names)) {
            if (name == platform_only_names[j] and lib.link == "dynamic") {
                skip = true
                break
            }
            j = j + 1
        }
        if (skip) {
            i = i + 1
            continue
        }
        var p = lib.lib or ""
        if (p != "" and not starts_with(p, "-l") and not starts_with(p, "-framework")) {
            result = arr_push(result, make_build_path(p))
        }
        i = i + 1
    }

    // add base_libs that Python re-adds at the end for main exe
    var base_lib_names = ["mpdec", "utf8proc", "mir"]
    i = 0
    while (i < len(base_lib_names)) {
        var name = base_lib_names[i]
        var lib = find_ext_lib(ext_libs, name)
        if (lib != null and lib.link != "dynamic" and lib.link != "none") {
            var p = lib.lib or ""
            if (p != "") {
                result = arr_push(result, make_build_path(p))
            }
        }
        i = i + 1
    }

    // nghttp2 with force_load (macOS only)
    if (platform == "macos") {
        var nghttp2 = find_ext_lib(ext_libs, "nghttp2")
        if (nghttp2 != null and nghttp2.link != "dynamic" and nghttp2.link != "none") {
            var p = nghttp2.lib or ""
            if (p != "") {
                result = arr_push(result, "-Wl,-force_load," ++ make_build_path(p))
            }
        }
    } else {
        // Linux/Windows: add nghttp2 normally
        var nghttp2 = find_ext_lib(ext_libs, "nghttp2")
        if (nghttp2 != null and nghttp2.link != "dynamic" and nghttp2.link != "none") {
            var p = nghttp2.lib or ""
            if (p != "") {
                result = arr_push(result, make_build_path(p))
            }
        }
    }

    // curl at the end (Python re-adds it)
    var curl = find_ext_lib(ext_libs, "curl")
    if (curl != null and curl.link == "static") {
        var p = curl.lib or ""
        if (p != "") {
            result = arr_push(result, make_build_path(p))
        }
    }

    // libedit (macOS/Linux only, not Windows)
    if (platform != "windows") {
        var libedit = find_ext_lib(ext_libs, "libedit")
        if (libedit != null and libedit.link == "static") {
            var p = libedit.lib or ""
            if (p != "") {
                result = arr_push(result, make_build_path(p))
            }
        }
    }

    // Linux: add OpenGL and OpenMP libraries as Lua comment + -l flags
    if (platform == "linux") {
        result = arr_push(result, "-- OpenGL and OpenMP libraries (required by ThorVG)")
        result = arr_push(result, "-lGL")
        result = arr_push(result, "-lGLU")
        result = arr_push(result, "-lgomp")
    }

    return result
}

// Build dynamic libraries for main exe
pn build_exe_dyn_libs(config, ext_libs, platform) {
    var result = []
    var exe_deps = build_exe_deps_order(config, platform)

    // collect dynamic libraries in exe_deps order
    var i = 0
    while (i < len(exe_deps)) {
        var name = exe_deps[i]
        var lib = find_ext_lib(ext_libs, name)
        if (lib != null and lib.link == "dynamic") {
            var lib_path = lib.lib or ""
            // skip framework libs
            if (starts_with(lib_path, "-framework ")) {
                i = i + 1
                continue
            }
            if (starts_with(lib_path, "-l")) {
                result = arr_push(result, slice(lib_path, 2, len(lib_path)))
            } else if (lib_path != "") {
                result = arr_push(result, lib_path)
            } else {
                result = arr_push(result, name)
            }
        }
        i = i + 1
    }

    return result
}

// Build Windows-specific defines for main exe
fn build_exe_win_defines() {
    ["WIN32", "_WIN32", "NATIVE_WINDOWS_BUILD", "CURL_STATICLIB", "UTF8PROC_STATIC"]
}

// ============================================================
// === Input-full source file ordering ===
// ============================================================

// Reorder source files: C++ first, then C (matching Python behavior)
pn reorder_input_files(source_files) {
    var cpp_files = []
    var c_files = []
    var i = 0
    while (i < len(source_files)) {
        var f = source_files[i]
        if (ends_with(f, ".cpp")) {
            cpp_files = arr_push(cpp_files, f)
        } else if (ends_with(f, ".c")) {
            c_files = arr_push(c_files, f)
        } else {
            cpp_files = arr_push(cpp_files, f)
        }
        i = i + 1
    }
    // combine: cpp first, then c
    return cpp_files ++ c_files
}

// ============================================================
// === Test data computation ===
// ============================================================

pn build_test_sources(test_entry) {
    var result = []
    var source = test_entry.source or ""
    if (source != "") {
        var src_path = if (starts_with(source, "test/")) source else "test/" ++ source
        result = arr_push(result, src_path)
    }
    var add_srcs = test_entry.additional_sources or []
    var i = 0
    while (i < len(add_srcs)) {
        result = arr_push(result, add_srcs[i])
        i = i + 1
    }
    return result
}

pn build_test_link_names(test_entry, ext_libs, platform) {
    var result = []
    var deps = test_entry.dependencies or []
    var i = 0
    while (i < len(deps)) {
        var d = deps[i]
        if (d == "lambda-input-full") {
            result = arr_push(result, "lambda-input-full-cpp")
            result = arr_push(result, "lambda-lib")
        } else {
            result = arr_push(result, d)
        }
        i = i + 1
    }

    // Add dynamic libraries from test libraries (e.g. freetype, zlib, bzip2 on Linux)
    if (platform == "linux" or platform == "windows") {
        var test_libs = test_entry.libraries or []
        i = 0
        while (i < len(test_libs)) {
            var name = test_libs[i]
            if (name != "rpmalloc" and name != "utf8proc" and name != "gtest" and name != "gtest_main") {
                var lib = find_ext_lib(ext_libs, name)
                if (lib != null and lib.link == "dynamic") {
                    result = arr_push(result, name)
                }
            }
            i = i + 1
        }
    }

    // On Linux/Windows, static libs (rpmalloc, utf8proc) use late-binding syntax in links
    if (platform == "linux" or platform == "windows") {
        var test_libs2 = test_entry.libraries or []
        i = 0
        while (i < len(test_libs2)) {
            var name = test_libs2[i]
            if (name == "rpmalloc" or name == "utf8proc") {
                var lib = find_ext_lib(ext_libs, name)
                if (lib != null and lib.link == "static") {
                    result = arr_push(result, ":lib" ++ name ++ ".a")
                }
            }
            i = i + 1
        }
    }
    return result
}

pn build_test_lib_paths(test_entry, ext_libs, platform) {
    var result = []
    var test_libs = test_entry.libraries or []
    var i = 0
    while (i < len(test_libs)) {
        var name = test_libs[i]
        // On Linux/Windows, rpmalloc and utf8proc are handled via late-binding in links
        if ((platform == "linux" or platform == "windows") and (name == "rpmalloc" or name == "utf8proc")) {
            i = i + 1
            continue
        }
        var lib = find_ext_lib(ext_libs, name)
        if (lib != null and lib.link == "static") {
            var p = lib.lib or ""
            if (p != "") {
                result = arr_push(result, make_build_path(p))
            }
        }
        i = i + 1
    }
    return result
}

pn has_dependency(test_entry, dep_name) {
    var deps = test_entry.dependencies or []
    var i = 0
    while (i < len(deps)) {
        if (deps[i] == dep_name) { return true }
        i = i + 1
    }
    return false
}

// split special_flags string into array of flags
pn build_extra_flags(flags_str) {
    if (flags_str == null or flags_str == "") { return [] }
    return split(flags_str, " ")
}

// check if test source file exists
pn test_source_exists(test_entry) {
    var source = test_entry.source or ""
    if (source == "") { return false }
    var path = if (starts_with(source, "test/")) source else "test/" ++ source
    return exists(path)
}

// Build force-load/whole-archive options for test with input-full dependency
pn build_force_load_opts(ext_libs, platform) {
    var ts_libs = ["tree-sitter-lambda", "tree-sitter"]
    if (platform == "linux") {
        // Linux: use --whole-archive
        var result = ["-Wl,--whole-archive"]
        var i = 0
        while (i < len(ts_libs)) {
            var lib = find_ext_lib(ext_libs, ts_libs[i])
            if (lib != null) {
                var p = lib.lib or ""
                if (p != "") {
                    result = arr_push(result, make_build_path(p))
                }
            }
            i = i + 1
        }
        result = arr_push(result, "-Wl,--no-whole-archive")
        return result
    } else if (platform == "macos") {
        // macOS: use -force_load for each library
        var result = []
        var i = 0
        while (i < len(ts_libs)) {
            var lib = find_ext_lib(ext_libs, ts_libs[i])
            if (lib != null) {
                var p = lib.lib or ""
                if (p != "") {
                    result = arr_push(result, "-Wl,-force_load," ++ make_build_path(p))
                }
            }
            i = i + 1
        }
        return result
    } else {
        // Windows: just link normally
        var result = []
        var i = 0
        while (i < len(ts_libs)) {
            var lib = find_ext_lib(ext_libs, ts_libs[i])
            if (lib != null) {
                var p = lib.lib or ""
                if (p != "") {
                    result = arr_push(result, make_build_path(p))
                }
            }
            i = i + 1
        }
        return result
    }
}

// Build input-full linkoptions for test projects (link-groups etc)
pn build_input_full_test_linkopts(config, ext_libs, platform) {
    var result = []

    if (platform == "linux") {
        result = arr_push(result, "-Wl,--start-group")
        result = arr_push(result, "-Wl,--end-group")
    } else if (platform == "windows") {
        result = arr_push(result, "-Wl,--allow-multiple-definition")
        // add Windows-specific library paths
        var win_libs = [
            "../../lambda/tree-sitter/libtree-sitter.a",
            "../../lambda/tree-sitter-lambda/libtree-sitter-lambda.a",
            "../../win-native-deps/lib/libmir.a",
            "/mingw64/lib/libmpdec.a",
            "../../win-native-deps/lib/libutf8proc.a",
            "/mingw64/lib/libmbedtls.a",
            "/mingw64/lib/libmbedx509.a",
            "/mingw64/lib/libmbedcrypto.a"
        ]
        var i = 0
        while (i < len(win_libs)) {
            result = arr_push(result, win_libs[i])
            i = i + 1
        }
        // add dynamic libs
        var dyn_flags = ["-lcurl", "-lnghttp2", "-lz", "-lbz2", "-lfreetype", "-lpng"]
        i = 0
        while (i < len(dyn_flags)) {
            result = arr_push(result, dyn_flags[i])
            i = i + 1
        }
        // add base_libs
        var base_libs = ["mpdec", "utf8proc", "mir", "nghttp2", "curl", "ssl", "crypto"]
        i = 0
        while (i < len(base_libs)) {
            var lib = find_ext_lib(ext_libs, base_libs[i])
            if (lib != null and lib.link != "dynamic" and lib.link != "none") {
                var p = lib.lib or ""
                if (p != "") {
                    result = arr_push(result, make_build_path(p))
                }
            }
            i = i + 1
        }
    }

    // Linux: add start/end group for tests with input-full dependency
    if (platform == "linux") {
        result = ["-Wl,--start-group"]
        result = arr_push(result, "-Wl,--end-group")
    }

    return result
}

// Build dynamic libs for test with input-full dependency
pn build_input_full_test_dyn_libs(ext_libs, platform) {
    var result = []

    // collect all dynamic external libraries
    var i = 0
    while (i < len(ext_libs)) {
        var lib = ext_libs[i]
        if (lib.link == "dynamic") {
            var lib_path = lib.lib or ""
            // skip framework libs
            if (starts_with(lib_path, "-framework ")) {
                i = i + 1
                continue
            }
            if (starts_with(lib_path, "-l")) {
                result = arr_push(result, slice(lib_path, 2, len(lib_path)))
            } else if (lib_path != "") {
                result = arr_push(result, lib_path)
            } else {
                result = arr_push(result, lib.name)
            }
        }
        i = i + 1
    }

    // add ncurses (libedit dependency, not on Windows)
    if (platform != "windows") {
        result = arr_push(result, "ncurses")
    }

    // Linux: add rpmalloc inside links block (must come after shared libraries)
    if (platform == "linux") {
        var rpmalloc = find_ext_lib(ext_libs, "rpmalloc")
        if (rpmalloc != null) {
            result = arr_push(result, ":librpmalloc.a")
        }
    }

    return result
}

// ============================================================
// === Source File Enumeration ===
// ============================================================

// Enumerate .c and .cpp files in a single directory
// Returns files in glob order: .c files first, then .cpp files
pn enumerate_dir(dir_path) {
    var entries^err = input(dir_path, "dir")
    if (err) { return [] }
    var c_files = []
    var cpp_files = []
    var i = 0
    while (i < len(entries)) {
        let e = entries[i]
        let ext = e.extension or ""
        let name = e.name or ""
        if (ext == "c") {
            c_files = arr_push(c_files, dir_path ++ "/" ++ name)
        } else if (ext == "cpp") {
            cpp_files = arr_push(cpp_files, dir_path ++ "/" ++ name)
        }
        i = i + 1
    }
    // combine: c first, then cpp (matching Python glob order)
    return c_files ++ cpp_files
}

// Enumerate all source files from config source_dirs, applying exclusions
pn enumerate_sources(config, platform) {
    var source_dirs = config.source_dirs or []
    var excludes = config.exclude_source_files or []

    // add platform-specific exclusions
    var plats = config.platforms or {}
    var plat = if (platform == "macos") plats.macos or {}
               else if (platform == "linux") plats.linux or {}
               else plats.windows or {}
    var plat_excludes = plat.exclude_source_files or []
    excludes = excludes ++ plat_excludes

    // start with explicit source_files from config
    var all_files = config.source_files or []

    // enumerate each source directory
    var i = 0
    while (i < len(source_dirs)) {
        var dir_files = enumerate_dir(source_dirs[i])
        all_files = all_files ++ dir_files
        i = i + 1
    }

    // add platform-specific additional source files
    var plat_add = plat.additional_source_files or []
    all_files = all_files ++ plat_add

    // filter out excluded files
    var result = []
    i = 0
    while (i < len(all_files)) {
        if (not arr_contains(excludes, all_files[i])) {
            result = arr_push(result, all_files[i])
        }
        i = i + 1
    }

    return result
}

// ============================================================
// === Main Entry Point ===
// ============================================================

pn main() {
    print("Lambda Premake5 Generator v2")

    var config^err = input("build_lambda_config.json", "json")
    if (err != null) {
        print("Error: Failed to load config")
        return 1
    }

    // Parse platform from PLATFORM env var or detect automatically
    // Usage: PLATFORM=macos ./lambda.exe run utils/generate_premake_v2.ls
    var env_plat^env_err = cmd("printenv", ["PLATFORM"])
    var platform = ""
    if (env_err == null and env_plat != null) {
        // trim trailing newline from printenv output
        var ep = string(env_plat)
        if (ends_with(ep, "\n")) {
            ep = slice(ep, 0, len(ep) - 1)
        }
        if (ep == "macos" or ep == "mac" or ep == "darwin") {
            platform = "macos"
        } else if (ep == "linux" or ep == "lin") {
            platform = "linux"
        } else if (ep == "windows" or ep == "win") {
            platform = "windows"
        }
    }
    if (platform == "") {
        platform = detect_platform()
    }
    print("Platform: " ++ platform)

    // Build merged external libraries
    print("Building external library map...")
    var ext_libs = build_external_libs(config, platform)
    print("  ext_libs: " ++ string(len(ext_libs)) ++ " entries")

    // Compute build options from config + platform flags
    var build_opts = build_platform_build_opts(config, platform)
    print("  build_opts: " ++ string(len(build_opts)))

    // Check if ASAN should be disabled
    var plats = config.platforms or {}
    var disable_asan = false
    if (platform == "windows") {
        var win = plats.windows or {}
        disable_asan = win.disable_sanitizer or true
    } else if (platform == "linux") {
        var lin = plats.linux or {}
        disable_asan = lin.disable_sanitizer or false
    }
    print("  disable_asan: " ++ string(disable_asan))

    // Compute include lists
    print("Computing includes...")
    var lib_includes = build_lib_includes(config, platform, ext_libs)
    print("  lib_includes: " ++ string(len(lib_includes)))

    var input_includes = build_input_includes(config, platform, ext_libs)
    print("  input_includes: " ++ string(len(input_includes)))

    var full_includes = build_full_includes(config, platform, ext_libs)
    print("  full_includes: " ++ string(len(full_includes)))

    // Compute static paths
    print("Computing library paths...")
    var input_static_paths = build_input_static_paths(config, ext_libs, platform)
    print("  input_static_paths: " ++ string(len(input_static_paths)))

    var input_dyn_links = build_input_dyn_links(config, ext_libs, platform)
    print("  input_dyn_links: " ++ string(input_dyn_links))

    // Build platform-specific defines for input-full-cpp
    var targets = config.targets or []
    var t_input = find_target(targets, "lambda-input-full")
    var input_target_defines = if (t_input != null) t_input.defines or [] else []
    var input_defines = build_input_defines(config, platform, input_target_defines)

    // Enumerate source files for main exe (using input(dir, "dir"))
    print("Enumerating source files...")
    var exe_files = enumerate_sources(config, platform)
    print("  exe_files: " ++ string(len(exe_files)))

    var exe_static_paths = build_exe_static_paths(config, platform, ext_libs)
    print("  exe_static_paths: " ++ string(len(exe_static_paths)))

    var exe_dyn_libs = build_exe_dyn_libs(config, ext_libs, platform)
    print("  exe_dyn_libs: " ++ string(len(exe_dyn_libs)))

    // Build force-load options and input-full test linkopts
    var force_load_opts = build_force_load_opts(ext_libs, platform)
    var input_full_test_linkopts = build_input_full_test_linkopts(config, ext_libs, platform)
    var input_full_test_dyn_libs = build_input_full_test_dyn_libs(ext_libs, platform)

    // Windows-specific extra defines for main exe
    var exe_extra_defines = if (platform == "windows") build_exe_win_defines() else []

    // Build output section by section
    print("Generating output...")

    // Header + workspace + configs
    var out = gen_header(platform)
    out = out ++ gen_workspace(config)
    out = out ++ gen_debug_config(platform, config)
    if (platform == "macos") {
        out = out ++ gen_release_config_macos()
    } else if (platform == "linux") {
        out = out ++ gen_release_config_linux()
    } else {
        out = out ++ gen_release_config_windows()
    }
    out = out ++ gen_platform_globals()
    print("  header/workspace/configs OK")

    // lambda-lib
    var t_lib = find_target(targets, "lambda-lib")
    if (t_lib != null) {
        out = out ++ gen_lambda_lib(t_lib, lib_includes, platform, build_opts)
    }
    print("  lambda-lib OK")

    // lambda-input-full-cpp
    if (t_input != null) {
        // reorder source files: C++ first, then C
        var reordered = reorder_input_files(t_input.source_files or [])
        // create a copy of the target with reordered files
        out = out ++ gen_input_full({
            source_files: reordered,
            source_patterns: t_input.source_patterns or [],
            exclude_patterns: t_input.exclude_patterns or [],
            defines: input_defines,
            link: t_input.link or "static"
        }, input_includes, input_static_paths, input_dyn_links, platform, build_opts)
    }
    print("  lambda-input-full-cpp OK")

    // lambda-input-full wrapper
    out = out ++ gen_input_wrapper()
    print("  wrapper OK")

    // Main executable
    var exe_name = strip_exe(config.output or "lambda.exe")
    out = out ++ gen_lambda_exe(exe_name, exe_files, full_includes, exe_static_paths, platform, build_opts, exe_dyn_libs, exe_extra_defines)
    print("  main exe OK")

    // Test projects
    var test_cfg = config.test or {}
    var suites = test_cfg.test_suites or []
    var suite_idx = 0
    var test_count = 0
    var skip_count = 0
    while (suite_idx < len(suites)) {
        var suite = suites[suite_idx]
        var tests = suite.tests or []
        var special_flags = suite.special_flags or ""
        var test_idx = 0
        while (test_idx < len(tests)) {
            var t = tests[test_idx]
            var bin = t.binary or ""
            var proj_name = if (bin != "") strip_exe(bin) else "test_unknown"

            // skip tests whose source files don't exist
            if (not test_source_exists(t)) {
                skip_count = skip_count + 1
                test_idx = test_idx + 1
                continue
            }

            var sources = build_test_sources(t)
            var link_names = build_test_link_names(t, ext_libs, platform)
            var lib_paths = build_test_lib_paths(t, ext_libs, platform)
            var is_input = has_dependency(t, "lambda-input-full")
            var defs = t.defines or []
            // get special_flags from test entry (or suite level)
            var test_flags = t.special_flags or special_flags
            var extra = build_extra_flags(test_flags)
            // build combined buildoptions
            var all_opts = build_opts ++ extra

            if (is_input) {
                out = out ++ gen_test_input_full(proj_name, sources, full_includes, link_names, lib_paths, defs, all_opts, platform, disable_asan, input_full_test_linkopts, input_full_test_dyn_libs, force_load_opts)
            } else {
                out = out ++ gen_test_simple(proj_name, sources, full_includes, link_names, lib_paths, defs, all_opts, platform, disable_asan)
            }

            test_count = test_count + 1
            test_idx = test_idx + 1
        }
        suite_idx = suite_idx + 1
    }
    print("  " ++ string(test_count) ++ " test projects OK (" ++ string(skip_count) ++ " skipped)")

    // Write output
    var outpath = if (platform == "macos") "temp/premake5_v2.mac.lua"
                  else if (platform == "linux") "temp/premake5_v2.lin.lua"
                  else "temp/premake5_v2.win.lua"

    // trim trailing blank line
    if (len(out) > 1) {
        var last = slice(out, len(out) - 1, len(out))
        if (last == "\n") {
            out = slice(out, 0, len(out) - 1)
        }
    }

    out |> outpath

    print("Output: " ++ outpath)
    print("Size: " ++ string(len(out)) ++ " chars")
    print("Done!")

    return 0
}
