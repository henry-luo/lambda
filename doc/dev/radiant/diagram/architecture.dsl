workspace "Radiant" "HTML/CSS layout, rendering and interaction engine inside Lambda" {

  model {
    dev = person "Developer" "Runs layout/render/view/webdriver, or embeds Radiant."
    hostos = softwareSystem "Host OS / GPU / Windowing" "GLFW window, CoreGraphics/ThorVG raster, platform WebView, OS fonts and input." "External"

    lambda = softwareSystem "Lambda" "General-purpose data and document runtime with a MIR JIT." {
      cli = container "CLI" "Dispatches the layout, render, view and webdriver subcommands." "lambda/main.cpp"

      radiant = container "Radiant engine" "Turns HTML plus CSS into laid-out, painted, interactive documents." "radiant" {
        viewdom = component "View and DOM model" "The unified DomNode-is-View tree, view pool, incremental reflow, source-position bridge." "view.hpp, view_pool.cpp"
        css = component "CSS style resolution" "Used and computed-value resolution onto the view tree." "resolve_css_style.cpp, resolve_htm_style.cpp"
        layout = component "Layout engine" "Driver, block and BFC, inline and text, flex, grid, table, positioned, intrinsic sizing." "layout_*.cpp, intrinsic_sizing.cpp"
        render = component "Rendering" "Paint IR, display list, render walk, per-feature painters, raster and PDF export." "paint_ir.cpp, display_list*, render_*.cpp"
        vector = component "SVG and vector graphics" "RdtVector dual backend, inline SVG, diagram layout." "rdt_vector_*, render_svg_inline.cpp, graph_*"
        interaction = component "Interaction" "Events and input, animation, editing and selection, forms, interaction state." "event.cpp, state_store.cpp, dom_range.cpp, animation.cpp"
        scripting = component "JS scripting integration" "Runs page scripts and inline handlers over the DOM." "script_runner.cpp"
        shell = component "Shell and browsing" "UiContext window loop, browsing session, media, webview, WebDriver." "window.cpp, browsing_session.cpp, webview_*, webdriver"
      }

      shared = container "Shared Lambda runtime" "Item value model, GC, MIR JIT, MarkBuilder, input parsers and formatters, lib/font engine." "lambda core, lib/font"
      js = container "LambdaJS engine" "Compiles page JavaScript to MIR and binds it to the DOM and CSSOM." "lambda/js"
    }

    dev -> cli "runs layout / render / view / webdriver"
    cli -> radiant "load document, layout, render"
    radiant -> shared "Items, Mark data, input parsers, fonts, MIR JIT"
    scripting -> js "transpile and run page scripts"
    js -> viewdom "DOM and CSSOM bindings over DomElement"
    render -> hostos "surface, ThorVG or CoreGraphics raster"
    vector -> hostos "vector rasterization"
    interaction -> hostos "mouse, keyboard, IME"
    shell -> hostos "GLFW window, native WebView"

    css -> viewdom "writes computed style onto nodes"
    layout -> viewdom "tags views, fills geometry"
    layout -> css "reads computed style"
    render -> layout "walks the laid-out tree"
    render -> vector "vector and SVG paints"
    interaction -> viewdom "hit-test, mutate"
    interaction -> layout "reflow"
    interaction -> render "repaint"
    scripting -> viewdom "reads and mutates the DOM"
    shell -> layout "drives layout"
    shell -> render "drives paint"
    shell -> interaction "feeds input and frames"
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
    component radiant "c4_component" {
      include *
      autolayout lr
    }
  }
}
