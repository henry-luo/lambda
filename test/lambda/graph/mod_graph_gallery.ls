import document: lambda.graph.document
import tikz: lambda.doc.tikz.tikz
import dom
import updates: lambda.ui.core.publish

let UPSTREAM = input("test/lambda/graph/upstream/sources.json", "json")^;

fn render_tikz(path, width) any^ => tikz.render_with_program(input(path, "text")^,
    null, null, 0, false, {text_width_px:width, source_path:path});

// Relative TeX dimensions need the laid-out frame width; re-render when it changes.
pn prepare_tikz(owner) {
    var changed = false
    for (frame in dom.query_selector_all(owner, ".graph-frame")) {
        let graphic = dom.query_selector(frame, ".graph-content[data-tikz-source]")
        let box = if (graphic != null) dom.bounding_box(frame) else null
        if (box != null and box.width > 36.0 and box.height > 0) {
            let width = box.width - 36.0
            let width_key = string(width)
            if (dom.get_attribute(graphic, "data-tikz-width") != width_key) {
                let path = dom.get_attribute(graphic, "data-tikz-source")
                let picture = render_tikz(path, width) ^ { ^ }
                let content = if (picture is error)
                    <p class:"graph-error", "Could not render this graph: " ++ picture.message>
                    else picture
                // Insert the rendered fragment itself so its bounds are the drawing bounds.
                dom.set_inner_html(graphic, format(content, 'xml')^)
                dom.set_attribute(graphic, "data-tikz-width", width_key)
                changed = true
            }
        }
    }
    return changed
}

// Graph layout fills its parent width; its children give the actual drawing bounds.
fn drawing_bounds(graphic) {
    let origin = dom.bounding_box(graphic)
    let graph = dom.query_selector(graphic, ".lambda-graph")
    let graph_box = if (graph != null) dom.bounding_box(graph) else null
    let first = dom.first_element_child(graphic)
    let boxes = if (graph != null) [for (child in dom.children(graph), let box = dom.bounding_box(child)
        where (box != null and box.width > 0 and box.height > 0)) box]
        else if (first != null) [dom.bounding_box(first)] else []
    let valid = [for (box in boxes where (box != null and box.width > 0 and box.height > 0)) box]
    if (origin == null or len(valid) == 0) null else {
        // Edge paint can extend beyond every DOM child; the graph's laid-out height includes it.
        let graph_width_is_intrinsic = graph_box != null and graph_box.width < origin.width - 1.0
        let left = min([for (box in valid) box.x,
            if (graph_width_is_intrinsic) graph_box.x else origin.x + origin.width]) - origin.x
        let top = min([for (box in valid) box.y,
            if (graph_box != null) graph_box.y else origin.y + origin.height]) - origin.y
        let right = max([for (box in valid) box.x + box.width,
            if (graph_width_is_intrinsic) graph_box.x + graph_box.width else origin.x]) - origin.x
        let bottom = max([for (box in valid) box.y + box.height,
            if (graph_box != null) graph_box.y + graph_box.height else origin.y]) - origin.y
        {x:left, y:top, width:right-left, height:bottom-top}
    }
}

// Measure after layout because graph and TikZ labels are laid out by Radiant.
pn set_graph_zoom(graphic, zoom) {
    let graph = dom.query_selector(graphic, ".lambda-graph")
    if (graph == null) { return null }
    // Custom graph layout reads spacing in pixels, so keep it in step with layout zoom.
    for (key in ["node-sep", "rank-sep", "edge-sep"]) {
        let attr = "data-" ++ key
        let saved = "data-unscaled-" ++ key
        let original = dom.get_attribute(graphic, saved) or dom.get_attribute(graph, attr)
        if (original != null) {
            dom.set_attribute(graphic, saved, original)
            dom.set_attribute(graph, attr, string(float(original) * zoom)^)
        }
    }
    dom.set_attribute(graph, "data-graph-zoom", string(zoom)^)
}

fn measure_fit(frame) {
    let graphic = dom.query_selector(frame, ".graph-content")
    if (graphic == null) { return null }
    let frame_box = dom.bounding_box(frame)
    if (frame_box == null or frame_box.width <= 0 or frame_box.height <= 0) { return null }
    let viewer = dom.closest(frame, "#graph-viewer") != null
    let drawing = if (viewer) drawing_bounds(graphic) else null
    let bounds = if (drawing != null) drawing else {
        let scroll = dom.scroll_metrics(graphic);
        {x:0.0,y:0.0,width:float(scroll.scroll_width),height:float(scroll.scroll_height)}
    }
    let width = max([1.0, bounds.width])
    let height = max([1.0, bounds.height])
    let scale = min([if (viewer) 100.0 else 1.0, max([1.0, frame_box.width - 36.0]) / width,
        max([1.0, frame_box.height - 36.0]) / height])
    if (viewer and scale > 1.0 and dom.query_selector(graphic, ".lambda-graph") != null) {
        return {graphic:graphic, zoom:scale, transform:null}
    }
    return {graphic:graphic, zoom:null,
        transform:"translate(" ++ string((frame_box.width - width * scale) / 2.0 - bounds.x * scale) ++
            "px," ++ string((frame_box.height - height * scale) / 2.0 - bounds.y * scale) ++ "px) scale(" ++ string(scale) ++ ")"}
}

pn fit_graphs(owner) {
    // Finish measuring the whole gallery before changing any styles.
    let fits = [for (frame in dom.query_selector_all(owner, ".graph-frame"), let fit = measure_fit(frame)^
        where ((fit != null) ^ { false })) fit]^
    var needs_center = false
    for (fit in fits) {
        if (fit.zoom != null) {
            // Layout zoom requests larger glyphs; scaling the painted layer enlarges glyph bitmaps.
            set_graph_zoom(fit.graphic, fit.zoom)^
            let scale = string(fit.zoom)^
            dom.style_set_property(fit.graphic, "zoom", scale)
            dom.set_attribute(fit.graphic, "data-gallery-zoom", scale)
            needs_center = true
            continue
        }
        updates.style(fit.graphic, "transform", fit.transform)^
    }
    return needs_center
}

pn center_zoomed(owner) {
    let frame = dom.query_selector(owner, "#graph-viewer .graph-frame")
    let graphic = dom.query_selector(frame, ".graph-content[data-gallery-zoom]")
    if (graphic != null) {
        let zoom = float(dom.get_attribute(graphic, "data-gallery-zoom"))
        let bounds = drawing_bounds(graphic)
        let box = dom.bounding_box(frame)
        if (bounds != null and box != null) updates.style(graphic, "transform",
            "translate(" ++ string(((box.width - bounds.width) / 2.0 - bounds.x) / zoom) ++
            "px," ++ string(((box.height - bounds.height) / 2.0 - bounds.y) / zoom) ++ "px)")^
    }
}

// Clone the selected graph so returning to the list preserves its layout and scroll position.
pn show_graph(owner, index) {
    let cards = dom.query_selector_all(owner, ".graph-card")
    if (index < 0 or index >= len(cards)^) { return null }
    let card = cards[index]
    let viewer = dom.get_element_by_id(owner, "graph-viewer")
    let frame = dom.query_selector(viewer, ".graph-frame")
    let graphic = dom.clone_node(dom.query_selector(card, ".graph-content"), true)
    dom.style_set_property(graphic, "transform", "none")
    dom.set_inner_html(frame, "")
    dom.append_child(frame, graphic)
    dom.set_text_content(dom.get_element_by_id(owner, "viewer-title"), dom.text_content(dom.query_selector(card, "h2")))
    dom.set_text_content(dom.get_element_by_id(owner, "viewer-path"), dom.text_content(dom.query_selector(card, "code")))
    dom.set_text_content(dom.get_element_by_id(owner, "viewer-count"), string(index + 1) ++ " / " ++ string(len(cards)^)^)
    dom.style_set_property(dom.get_element_by_id(owner, "viewer-source"), "display", "none")
    dom.style_set_property(dom.get_element_by_id(owner, "viewer-stage"), "display", "flex")
    dom.set_text_content(dom.get_element_by_id(owner, "viewer-source-button"), "View source")
    for (control in ["previous", "next"]) {
        let button = dom.get_element_by_id(owner, "viewer-" ++ control)
        if ((control == "previous" and index == 0) or (control == "next" and index == len(cards)^ - 1))
            dom.set_attribute(button, "disabled", "")
        else dom.remove_attribute(button, "disabled")
    }
    dom.style_set_property(dom.get_element_by_id(owner, "gallery-list"), "display", "none")
    dom.style_set_property(viewer, "display", "block")
    dom.scroll_into_view(viewer)
    dom.request_frame(viewer, "gallery_fit")
}

pn show_list(owner, index) {
    let cards = dom.query_selector_all(owner, ".graph-card")
    dom.style_set_property(dom.get_element_by_id(owner, "graph-viewer"), "display", "none")
    dom.style_set_property(dom.get_element_by_id(owner, "gallery-list"), "display", "block")
    if (index >= 0 and index < len(cards)^) dom.scroll_into_view(cards[index])
    dom.request_frame(dom.get_element_by_id(owner, "gallery-list"), "gallery_fit")
}

view <gallery_document> state selected:-1 { ~.rendered }
on load(evt) {
    dom.request_frame(dom.get_element_by_id(evt.target, "gallery-list"), "gallery_fit")
    return 'handled'
}
on resize(evt) {
    let viewer = dom.get_element_by_id(evt.target, "graph-viewer")
    let graphic = dom.query_selector(viewer, ".graph-content")
    let viewing = graphic != null and dom.computed_style(viewer, "display") != "none"
    if (viewing) {
        dom.style_set_property(graphic, "transform", "none")
        dom.style_set_property(graphic, "zoom", "1")
        set_graph_zoom(graphic, 1.0)^
        dom.remove_attribute(graphic, "data-gallery-zoom")
    }
    dom.request_frame(if (viewing) viewer else dom.get_element_by_id(evt.target, "gallery-list"), "gallery_fit")
    return 'pass'
}
on gallery_fit(evt) {
    if (prepare_tikz(evt.target)^) dom.request_frame(evt.target, "gallery_fit")
    else if (fit_graphs(evt.target)^) dom.request_frame(evt.target, "gallery_center")
    return 'handled'
}
on gallery_center(evt) {
    center_zoomed(evt.target)^
    return 'handled'
}
on click(evt) {
    let owner = dom.root_node(evt.target)
    let action = dom.closest(evt.target, "[data-gallery-action]")
    if (action != null) {
        let command = dom.get_attribute(action, "data-gallery-action")
        if (command == "back") { show_list(owner, selected)^; selected = -1 }
        else if (command == "source") {
            let source = dom.get_element_by_id(owner, "viewer-source")
            let showing = dom.computed_style(source, "display") != "none"
            if (not showing) {
                let path = dom.text_content(dom.get_element_by_id(owner, "viewer-path"))
                let text = input(path, 'text') ^ { "Unable to read source: " ++ ^.message }
                dom.set_text_content(source, text)
            }
            dom.style_set_property(source, "display", if (showing) "none" else "block")
            dom.style_set_property(dom.get_element_by_id(owner, "viewer-stage"), "display", if (showing) "flex" else "none")
            dom.set_text_content(action, if (showing) "View source" else "View graph")
            if (showing) {
                let graphic = dom.query_selector(dom.get_element_by_id(owner, "graph-viewer"), ".graph-content")
                dom.style_set_property(graphic, "transform", "none")
                dom.style_set_property(graphic, "zoom", "1")
                set_graph_zoom(graphic, 1.0)^
                dom.remove_attribute(graphic, "data-gallery-zoom")
                dom.request_frame(dom.get_element_by_id(owner, "graph-viewer"), "gallery_fit")
            }
        }
        else if (command == "previous" and selected > 0) { selected = selected - 1; show_graph(owner, selected)^ }
        else if (command == "next" and selected + 1 < len(dom.query_selector_all(owner, ".graph-card"))^) {
            selected = selected + 1; show_graph(owner, selected)^
        }
        return 'handled'
    }
    let card = dom.closest(evt.target, ".graph-card")
    if (card == null) { return 'pass' }
    let cards = dom.query_selector_all(owner, ".graph-card")
    for (i, candidate in cards) if (dom.same_node(card, candidate)) {
        selected = i
        show_graph(owner, selected)^
        return 'handled'
    }
    return 'pass'
}
on keydown(evt) {
    let card = dom.closest(evt.target, ".graph-card")
    if (card != null and (evt.key == "Enter" or evt.key == " ")) {
        dom.dispatch(card, {type:"click",bubbles:true})
        return 'handled'
    }
    return 'pass'
}

pub fn page(title, caption, body) => <html lang:"en",
    <head
        <meta charset:"utf-8">; <meta name:"viewport",content:"width=device-width,initial-scale=1">
        <title title ++ " · Graph gallery">; <link rel:"stylesheet",href:"test_graph.css">
    >
    apply(<gallery_document rendered:<body
    <main id:"gallery-list",
        <nav class:"breadcrumbs",<a href:"test_graph.ls","Graph gallery">; <span " / ">; <span title>>
        <header <p class:"eyebrow","LAMBDA / DATA & DIAGRAMS">; <h1 title>; <p class:"intro",caption>>
        body;
        <footer <a href:"test_graph.ls","← Back to graph gallery">>
    >
    <main id:"graph-viewer",style:"display:none;",
        <nav class:"viewer-nav",'aria-label':"Graph viewer",
            <button type:"button",class:"viewer-back",'data-gallery-action':"back","← Back to list">
            <div class:"viewer-nav-actions",
                <button id:"viewer-source-button",type:"button",class:"viewer-source-button",'data-gallery-action':"source","View source">
                <span id:"viewer-count">>>
        <header class:"viewer-heading",<h1 id:"viewer-title">; <code id:"viewer-path">>
        <div id:"viewer-stage",class:"viewer-stage",
            <button id:"viewer-previous",type:"button",class:"viewer-arrow",'data-gallery-action':"previous",'aria-label':"Previous graph","‹">
            <div class:"graph-frame">
            <button id:"viewer-next",type:"button",class:"viewer-arrow",'data-gallery-action':"next",'aria-label':"Next graph","›">
        >
        <pre id:"viewer-source",style:"display:none;">
    >>>)>;

fn source_name(path) {
    let parts = split(path, "/");
    parts[len(parts) - 1]
}

fn card(title, path, image, is_tikz) => <section class:"graph-card",role:"button",tabindex:"0",'aria-label':"Open " ++ title,
    <header <h2 title>; <code path>>
    <div class:"graph-frame",<div class:"graph-content",*:(if (is_tikz) {'data-tikz-source':path} else {}),image>>
>;

fn render_graph(path, flavor, view_key) any^ =>
    document.to_html(input(path, {type:"graph",flavor:flavor})^,
        {theme:"light",view_key:view_key});

pub fn source_card(title, path, flavor, view_key = null) any^ {
    let is_tikz = flavor == "tikz";
    let image = if (is_tikz) null else (render_graph(path, flavor, view_key) ^ { ^ });
    let preview = if (image is error) <p class:"graph-error","Could not render this graph: " ++ image.message> else image;
    card(title, path, preview, is_tikz)
}

pub fn input_cards(paths, flavor) any^ => [for (path in paths)
    source_card(source_name(path), path, flavor)];

pub fn upstream_cards(family) any^ {
    let fixtures = [for (entry in UPSTREAM.fixtures
        where (starts_with(entry.file, family ++ "/")^)) entry];
    [for (entry in fixtures)
        if (entry.views != null)
            for (view_key in entry.views)
                source_card(source_name(entry.file) ++ " · " ++ view_key,
                    "test/lambda/graph/upstream/" ++ entry.file, family, view_key)
        else source_card(source_name(entry.file),
            "test/lambda/graph/upstream/" ++ entry.file, family)]
}
