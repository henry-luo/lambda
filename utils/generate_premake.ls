// generate_premake.ls - Lambda Script Premake5 Generator
// Usage: ./lambda.exe run utils/generate_premake.ls
// Note: Workaround for Lambda bug where /* and */ in string literals
// are treated as block comments. We use "/"++STAR++"*" concatenation.

// WORKAROUND: Define glob components separately to avoid /* in strings
let STAR = "*"

fn detect_platform() {
    let p = sys.os.platform
    if (p == "darwin") "macos"
    else if (p == "linux") "linux"
    else "windows"
}

fn lua_string(s: string) {
    "\"" ++ s ++ "\""
}

fn strip_exe(name: string) {
    let n = len(name)
    if (n > 4 and ends_with(name, ".exe")) slice(name, 0, n - 4)
    else name
}

fn filter_dirs(dirs, prefix: string) {
    [for (d in dirs where starts_with(d, prefix)) d]
}

// Build glob pattern: dir/*.ext
fn make_glob(dir: string, ext: string) {
    dir ++ "/" ++ STAR ++ "." ++ ext
}

// Build recursive glob pattern: dir/**/*.ext  
fn make_rec_glob(dir: string, ext: string) {
    dir ++ "/" ++ STAR ++ STAR ++ "/" ++ STAR ++ "." ++ ext
}

fn make_file_entry(d: string) {
    "        " ++ lua_string(make_glob(d, "c")) ++ ",\n" ++
    "        " ++ lua_string(make_glob(d, "cpp")) ++ ","
}

fn make_include_entry(d: string) {
    "        " ++ lua_string(d) ++ ","
}

fn make_src_entry(s: string) {
    "        " ++ lua_string(s) ++ ","
}

fn make_lib_entry(lib) {
    (let p = lib.lib,
    let full = if (starts_with(p, "/")) p else "../../" ++ p,
    "        " ++ lua_string(full) ++ ",")
}

fn gen_files(dirs) {
    let items = [for (d in dirs) make_file_entry(d)]
    "    files {\n" ++ str_join(items, "\n") ++ "\n    }\n\n"
}

fn gen_includedirs(incl) {
    (let base = [".", "lib", "lambda", "include"],
    let all = [for (d in base) make_include_entry(d)],
    let ext = [for (d in incl) make_include_entry(d)],
    "    includedirs {\n" ++ str_join(all, "\n") ++ "\n" ++ str_join(ext, "\n") ++ "\n    }\n\n")
}

fn get_static_libs(libs) {
    (let static_libs = [for (lib in libs where lib.link == "static" and lib.lib != null) lib],
    let paths = [for (lib in static_libs) make_lib_entry(lib)],
    str_join(paths, "\n"))
}

fn gen_header(platform: string) {
    "-- Generated for " ++ platform ++ "\n\n"
}

fn gen_workspace(c, platform: string) {
    (let ws = c.workspace_name or "Lambda",
    let out = c.output or "lambda.exe",
    let start = strip_exe(out),
    let comp = c.compiler or "clang",
    "workspace " ++ lua_string(ws) ++ "\n" ++
    "    configurations { \"Debug\", \"Release\" }\n" ++
    "    platforms { \"native\" }\n" ++
    "    location \"build/premake\"\n" ++
    "    startproject " ++ lua_string(start) ++ "\n" ++
    "    toolset " ++ lua_string(comp) ++ "\n" ++
    "    cppdialect \"C++17\"\n" ++
    "    cdialect \"C99\"\n" ++
    "    warnings \"Extra\"\n\n")
}

fn gen_debug_config() {
    "    filter \"configurations:Debug\"\n" ++
    "        defines { \"DEBUG\" }\n" ++
    "        symbols \"On\"\n" ++
    "        optimize \"Off\"\n\n"
}

fn gen_release_config(platform: string) {
    let link = if (platform == "macos") "-Wl,-dead_strip" else "-Wl,--gc-sections"
    "    filter \"configurations:Release\"\n" ++
    "        defines { \"NDEBUG\" }\n" ++
    "        symbols \"Off\"\n" ++
    "        optimize \"On\"\n" ++
    "        buildoptions { \"-flto=thin\", \"-ffunction-sections\", \"-fdata-sections\" }\n" ++
    "        linkoptions { \"-flto=thin\", \"" ++ link ++ "\" }\n\n" ++
    "    filter {}\n\n"
}

fn gen_project(name: string, kind: string, lang: string, dir: string) {
    "project " ++ lua_string(name) ++ "\n" ++
    "    kind " ++ lua_string(kind) ++ "\n" ++
    "    language " ++ lua_string(lang) ++ "\n" ++
    "    targetdir " ++ lua_string(dir) ++ "\n" ++
    "    objdir \"build/obj/%{prj.name}\"\n\n"
}

fn gen_removefiles(patterns) {
    let items = [for (p in patterns) "        " ++ lua_string(p) ++ ","]
    "    removefiles {\n" ++ str_join(items, "\n") ++ "\n    }\n"
}

fn gen_lambda_lib(c, platform: string) {
    (let inc = c.includes or [],
    gen_project("lambda-lib", "StaticLib", "C", "build/lib") ++
    gen_files(["lib", "lib/serve"]) ++
    gen_removefiles([make_rec_glob("lib", "h")]) ++
    gen_includedirs(inc) ++
    "    buildoptions { \"-pedantic\", \"-fms-extensions\" }\n\n")
}

fn gen_lambda_runtime(c, platform: string) {
    (let src = c.source_dirs or [],
    let dirs = filter_dirs(src, "lambda"),
    let inc = c.includes or [],
    gen_project("lambda-runtime-cpp", "StaticLib", "C++", "build/lib") ++
    gen_files(dirs) ++
    gen_removefiles([make_rec_glob("lambda", "h"), make_rec_glob("lambda", "hpp"),
                     "lambda/main.cpp", "lambda/main-repl.cpp"]) ++
    gen_includedirs(inc) ++
    "    buildoptions { \"-std=c++17\" }\n\n")
}

fn gen_radiant(c, platform: string) {
    (let src = c.source_dirs or [],
    let dirs = filter_dirs(src, "radiant"),
    let inc = c.includes or [],
    if (len(dirs) == 0) ""
    else gen_project("radiant", "StaticLib", "C++", "build/lib") ++
         gen_files(dirs) ++
         gen_removefiles([make_rec_glob("radiant", "h"), make_rec_glob("radiant", "hpp")]) ++
         gen_includedirs(inc) ++
         "    buildoptions { \"-std=c++17\" }\n\n")
}

fn gen_main_exe(c, platform: string) {
    (let out = c.output or "lambda.exe",
    let name = strip_exe(out),
    let inc = c.includes or [],
    let libs = c.libraries or [],
    let links = get_static_libs(libs),
    let platlibs = if (platform == "macos") 
        "\"c++\", \"Cocoa.framework\", \"IOKit.framework\", \"CoreFoundation.framework\", \"OpenGL.framework\""
    else 
        "\"stdc++\", \"pthread\", \"dl\", \"m\"",
    gen_project(name, "ConsoleApp", "C++", ".") ++
    "    targetname " ++ lua_string(out) ++ "\n" ++
    "    files {\n        \"lambda/main.cpp\",\n        \"lambda/main-repl.cpp\",\n    }\n" ++
    gen_includedirs(inc) ++
    "    links { \"lambda-runtime-cpp\", \"lambda-lib\", \"radiant\", " ++ platlibs ++ " }\n" ++
    (if (links != "") "    linkoptions {\n" ++ links ++ "\n    }\n" else "") ++
    "    buildoptions { \"-std=c++17\" }\n\n")
}

fn gen_test(test, c, platform: string) {
    (let name = test.name or "unknown",
    let srcs = test.sources or [],
    let inc = c.includes or [],
    let libs = c.libraries or [],
    let links = get_static_libs(libs),
    let platlibs = if (platform == "macos") 
        "\"c++\", \"Cocoa.framework\", \"IOKit.framework\", \"CoreFoundation.framework\", \"OpenGL.framework\""
    else 
        "\"stdc++\", \"pthread\", \"dl\", \"m\"",
    let src_items = [for (s in srcs) make_src_entry(s)],
    gen_project(name, "ConsoleApp", "C++", "test") ++
    "    targetname " ++ lua_string(name ++ ".exe") ++ "\n" ++
    "    files {\n" ++ str_join(src_items, "\n") ++ "\n    }\n" ++
    gen_includedirs(inc) ++
    "    links { \"lambda-runtime-cpp\", \"lambda-lib\", \"radiant\", " ++ platlibs ++ " }\n" ++
    (if (links != "") "    linkoptions {\n" ++ links ++ "\n    }\n" else "") ++
    "    buildoptions { \"-std=c++17\" }\n\n")
}

fn gen_tests(c, platform: string) {
    let tests = c.tests or []
    if (len(tests) == 0) ""
    else (let items = [for (t in tests) gen_test(t, c, platform)],
          str_join(items, ""))
}

fn build_premake(c, platform: string) {
    gen_header(platform) ++
    gen_workspace(c, platform) ++
    gen_debug_config() ++
    gen_release_config(platform) ++
    gen_lambda_lib(c, platform) ++
    gen_lambda_runtime(c, platform) ++
    gen_radiant(c, platform) ++
    gen_main_exe(c, platform) ++
    gen_tests(c, platform)
}

pn main() {
    print("Lambda Premake5 Generator")
    
    var config = input("build_lambda_config.json", "json")
    if (config == null) {
        print("Error: Failed to load config")
        return 1
    }
    
    var platform = detect_platform()
    print("Platform: " ++ platform)
    
    var content = build_premake(config, platform)
    
    if (content == null) {
        print("Error: build_premake returned null")
        return 1
    }
    
    var outpath = if (platform == "macos") "temp/premake5_generated.mac.lua"
                  else if (platform == "linux") "temp/premake5_generated.linux.lua"
                  else "temp/premake5_generated.windows.lua"
    
    content |> outpath
    
    print("Output: " ++ outpath)
    print("Size: " ++ string(len(content)) ++ " chars")
    
    return 0
}
