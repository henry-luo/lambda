workspace "Lambda Runtime" "Core language runtime of Lambda Script" {

  model {
    dev = person "Developer" "Writes, runs, and embeds Lambda scripts and documents."
    hostos = softwareSystem "Host OS / filesystem" "Files, processes, clock, stdout." "External"
    mirlib = softwareSystem "MIR library" "vnmakarov/mir: MIR IR, generator, and optional C2MIR front-end." "External"
    treesitter = softwareSystem "Tree-sitter" "Incremental parser; lambda grammar generates parser.c." "External"

    lambda = softwareSystem "Lambda" "General-purpose data and document runtime with a MIR JIT." {
      cli = container "CLI / REPL" "Dispatches subcommands, runs scripts, drives the REPL." "lambda/main.cpp, runner.cpp, main-repl.cpp"

      core = container "Core runtime engine" "Compiles Lambda to MIR and executes it." "lambda/ (core)" {
        frontend = component "Front-end" "Tree-sitter parse, typed AST build, build-time type inference." "parse.c, build_ast.cpp, ast.hpp"
        c2mir = component "C transpiler (legacy)" "AST to C source text, then c2mir to MIR. ifdef-gated." "transpile.cpp, transpile-call.cpp, lambda-embed.h"
        mirdirect = component "MIR Direct transpiler" "AST lowered straight to MIR IR; inline boxing; GC-root frames." "transpile-mir.cpp"
        jit = component "JIT integration" "Import resolution, MIR_link and MIR_gen, debug table." "mir.c"
        valuemodel = component "Value and type model" "Tagged Item, containers, shapes, static Type family." "lambda-data.hpp, lambda.h, lambda.hpp"
        memgc = component "Memory and GC" "Non-moving mark-sweep heap, nurseries, name and shape pools." "lib/gc, lambda-mem.cpp, name_pool, shape_pool"
        builtins = component "Runtime builtins" "C-ABI support library and the system-function registry." "lambda-eval.cpp, sys_func_registry.c"
        numstr = component "Numbers, strings, vectors" "Numeric tower, decimal, datetime, UTF strings, ArrayNum." "lambda-eval-num.cpp, lambda-decimal.cpp, utf_string.cpp, lambda-vector.cpp"
        errors = component "Error handling" "ItemError and LambdaError, propagation, stack traces." "lambda-error.cpp"
        markapi = component "Mark data API" "Build, read, and edit Lambda data; value printer." "mark_builder.cpp, mark_reader.cpp, mark_editor.cpp, print.cpp"
        proc = component "Procedural runtime" "pn procedures, in-place mutation, for-loops, safety analyzer." "lambda-proc.cpp, safety_analyzer.cpp"
      }

      io = container "Input / Output" "Format parsers and formatters (JSON, XML, HTML, Markdown, ...)." "lambda/input, lambda/format"
      validator = container "Schema validator" "Schema-based data validation." "lambda/validator"
      js = container "LambdaJS engine" "Embedded JavaScript runtime sharing the core value model and GC." "lambda/js"
      radiant = container "Radiant layout engine" "HTML and CSS layout and rendering." "radiant"
    }

    dev -> cli "runs .ls / convert / validate"
    cli -> core "transpile and run"
    core -> mirlib "MIR_new_insn, MIR_link, MIR_gen"
    core -> treesitter "parse source to CST"
    core -> hostos "read source, write output, fetch"
    cli -> io "convert / input parse"
    cli -> validator "validate"
    js -> core "shares Item, GC, MIR JIT"
    radiant -> io "DOM over parsed HTML and CSS"

    frontend -> mirdirect "typed AST"
    frontend -> c2mir "typed AST (legacy)"
    mirdirect -> jit "MIR module"
    c2mir -> jit "MIR module"
    mirdirect -> valuemodel "emits boxing of Items"
    mirdirect -> memgc "emits GC root frames"
    jit -> builtins "resolves fn_* imports"
    builtins -> valuemodel "operates on Items"
    builtins -> numstr "numeric, string, vector ops"
    builtins -> errors "raises and propagates"
    valuemodel -> memgc "heap and nursery allocation"
    markapi -> valuemodel "constructs containers"
    proc -> builtins "IO and mutation builtins"
  }

  views {
    systemContext lambda "c4_context" {
      include *
      autolayout lr
    }
    container lambda "c4_container" {
      include *
      autolayout lr
    }
    component core "c4_component" {
      include *
      autolayout lr
    }
  }
}
