workspace "LambdaJS" "Embedded JavaScript runtime inside Lambda" {

  model {
    dev = person "Developer" "Runs, embeds, or tests JS and Lambda code."
    npmreg = softwareSystem "npm registry" "Package source for the Node.js compatibility layer." "External"
    hostos = softwareSystem "Host OS / Network" "Files, sockets, processes, TLS via libuv and mbedTLS." "External"

    lambda = softwareSystem "Lambda" "General-purpose data and document runtime with a MIR JIT." {
      cli = container "CLI" "Dispatches the js and js-test-batch subcommands and document commands." "lambda/main.cpp"

      js = container "LambdaJS engine" "Compiles JavaScript to MIR and executes it." "lambda/js" {
        frontend = component "Front-end" "Tree-sitter parse, AST build, early errors, lexical scope." "build_js_ast, js_early_errors, js_scope"
        transpiler = component "MIR transpiler" "Multi-phase AST to MIR lowering, code generation, eval." "js_mir_*, transpile_js_mir"
        runtimecore = component "Runtime core" "Values, property and prototype system, functions, closures, exceptions." "js_runtime*, js_props, js_property_attrs"
        stdlib = component "Standard library" "Object, Array, String, RegExp, TypedArrays, collections, Proxy." "js_globals, js_regex_*, js_typed_array"
        asyncmod = component "Async and modules" "Promises, libuv event loop, ESM and CommonJS." "js_event_loop, js_job_queue, js_mir_module_batch_lowering"
        host = component "Host bridges" "DOM, CSSOM, events, fetch, and the Node.js compatibility modules." "js_dom*, js_cssom, js_fs, js_http"
      }

      shared = container "Shared Lambda runtime" "Item value model, GC heap and nursery, name pool, MIR JIT, JSON and URL." "lambda core"
      radiant = container "Radiant layout engine" "HTML and CSS layout and rendering." "radiant"
    }

    dev -> cli "runs .js or embeds"
    cli -> js "transpile_js_to_mir"
    js -> shared "Items, GC, MIR JIT, JSON and URL"
    js -> hostos "fs, net, tls, child_process"
    js -> npmreg "npm install"
    host -> radiant "DOM and CSSOM over DomElement"
    frontend -> transpiler "typed JsAstNode tree"
    transpiler -> runtimecore "emits MIR calls into js_* runtime"
    runtimecore -> stdlib "built-in method dispatch"
    runtimecore -> asyncmod "promise, await, module load"
    runtimecore -> host "exotic-object dispatch"
  }

  views {
    systemContext lambda "c4ctx" {
      include *
      autolayout lr
    }
    container lambda "c4cont" {
      include *
      autolayout lr
    }
    component js "c4comp" {
      include *
      autolayout lr
    }
  }
}
