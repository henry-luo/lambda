// The archive demo's layout (§9): the add and search bars, the library rail,
// the snapshot list over the job table, and the viewer with its tabs.
// `env` carries the library's file: URL; `shown` what the viewer has unpacked.
import model: .mod_model

fn on_if(flag) => if (flag) {'aria-pressed': "true"} else {'aria-pressed': "false"}
fn button(id, cls, action, value, label, extra) => <button id:id, type:"button", class:cls, 'data-action':action,
    'data-value':value or "", *:extra, label>

fn add_bar(st) {
    let kinds = [for (kc in model.KIND_CHOICES) <option value:kc[0], *:(if (st.force == kc[0]) {selected:""} else {}), kc[1]>]
    let chips = [for (fc in model.FORMAT_CHOICES) button("format-" ++ fc[0], "chip", "format", fc[0], fc[1], on_if(fc[0] in st.formats))]
    let parts = [<input id:"url", type:"text", class:"url", placeholder:"https://… or a local path", value:st.url,
            autocomplete:"off", 'aria-label':"URL to archive">,
        <select id:"force", class:"force", 'aria-label':"Kind", *kinds>, *chips,
        button("archive-button", "primary", "archive", null, "Archive", {})];
    <div class:"add-bar", *parts>
}

fn search_bar(st) => <div class:"search-bar",
    <input id:"search", type:"search", class:"search", placeholder:"Search the archive…", value:st.query,
        autocomplete:"off", 'aria-label':"Search the archive">;
    if (st.hits != null) button("clear-search", "text-button", "clear-search", null, "Clear", {}) else null;
    if (st.note != null) <span id:"note", class:"note", st.note> else null>

fn rail(st) {
    let kinds = [for (k in model.kind_counts(st.records)) <button id:"kind-" ++ k.kind, type:"button", class:"rail-row",
        'data-action':"kind", 'data-value':k.kind, *:on_if(st.kind == k.kind), <span k.label>; <span class:"count", string(k.count)>>]
    let sites = [for (s in model.site_counts(st.records)) <button type:"button", class:"rail-row site", 'data-action':"site",
        'data-value':s.site, *:on_if(st.site == s.site), <span s.site>; <span class:"count", string(s.count)>>]
    let rows = [<h2 "Library">, *kinds, <h2 "Sites">, *sites];
    <nav class:"rail", 'aria-label':"Library", *rows>
}

fn thumb(env, r) => if (r.outputs.thumb != null and r.outputs.thumb.status == "ok")
        <img class:"thumb", alt:"", src:env.root_url ++ "/snapshots/" ++ r.id ++ "/thumb.png">
    else <span class:"thumb blank", model.kind_label(r.kind)>

fn row(st, env, r) {
    let snippet = model.snippet_of(st, r.id)
    let meta = join([for (x in [model.site_of(r), model.kind_label(r.kind), model.date_label(r.archived)] where x != "") x], " · ");
    <button type:"button", class:"row", 'data-action':"select", 'data-value':r.id, 'data-kind':r.kind,
        *:(if (st.selected == r.id) {'aria-current':"true"} else {}),
        (thumb(env, r));
        <span class:"row-text",
            <span class:"row-title", r.title>
            <span class:"row-meta", meta>
            <span class:"badges", join(model.badges(r), " ")>;
            if (snippet != null) <span class:"snippet", snippet> else null>>
}

fn job(j) {
    let fraction = model.progress_fraction(j);
    <div class:"job " ++ j.status, 'data-tag':j.tag, 'data-status':j.status,
        <span class:"job-url", (if (j.op == 'refresh') "Re-run " ++ j.id else j.url)>
        <span class:"job-progress", model.progress_label(j)>;
        if (fraction != null and j.status == "running") <span class:"bar", <span class:"fill",
            style:"width:" ++ string(round(fraction * 100)) ++ "%">> else null;
        if (j.status == "failed") button(null, "text-button", "dismiss", j.tag, "Dismiss", {})
            else button(null, "text-button", "cancel", j.tag, "Cancel", {})>
}

fn middle(st, env) {
    let rows = model.visible(st)
    let entries = if (len(rows) == 0) [<p class:"empty", if (st.hits != null) "No snapshot matches." else if (st.loaded) "Nothing archived yet." else "Opening the library…">]
        else [for (r in rows) row(st, env, r)]
    let jobs = [<h2 "Jobs">, *[for (j in st.jobs) job(j)]];
    <section class:"middle",
        <div id:"list", class:"list", 'data-rows':string(len(rows)), *entries>
        <div id:"jobs", class:"jobs", 'data-count':string(len(st.jobs)), *jobs>>
}

fn tabs(st) => <div class:"tabs", role:"tablist", *[for (t in model.TABS) <button id:"tab-" ++ t[0], type:"button", class:"tab",
    role:"tab", 'data-action':"tab", 'data-value':t[0], 'aria-selected':string(st.tab == t[0]), t[1]>]>

fn info(r, env) {
    let outputs = [for (o in model.output_rows(r)) <tr class:"output " ++ o.status, <td o.output>; <td o.status>; <td o.message or "">;
        <td if (o.status == "ok") "" else button("rerun-" ++ o.output, "text-button", "rerun", o.output, "Re-run", {})>>]
    let facts = [["URL", r.url], ["Archived", r.archived], ["Kind", r.kind], ["Engine", r.engine.name],
        ["Size", model.size_label(r.zip_bytes)], ["Assets", string(r.assets.count)], ["Note", r.note]]
    let rows = [<tr <th "Output">; <th "Status">; <th "Message">; <th "">>, *outputs];
    <div id:"info", class:"info",
        <table class:"facts", *[for (f in facts where f[1] != null) <tr <th f[0]>; <td f[1]>>]>
        <table class:"outputs", *rows>>
}

fn content(st, env, r, shown) {
    if (shown == null) <p class:"empty", "Choose a snapshot.">
    else if (st.tab == "snapshot") <iframe id:"snapshot-frame", class:"frame", src:shown.url ++ "/index.html", title:r.title>
    else if (st.tab == "reader") (if (shown.readable) <iframe id:"reader-frame", class:"frame", src:shown.url ++ "/readable.html",
        title:r.title> else <p class:"empty", "No readable version: " ++ (r.outputs.readable.message or "not produced") ++ ".">)
    else if (st.tab == "markdown") (if (shown.markdown != null) <pre id:"markdown", class:"markdown", shown.markdown>
        else <p class:"empty", "No Markdown: " ++ (r.outputs.markdown.message or "not produced") ++ ".">)
    else info(r, env)
}

fn viewer(st, env, shown) {
    let r = model.selected(st);
    <section class:"viewer",
        (tabs(st));
        <div id:"viewer-body", class:"viewer-body", 'data-tab':st.tab, 'data-id':if (r == null) "" else r.id, content(st, env, r, shown)>;
        if (r == null) null
        else if (st.confirm == r.id) <div class:"actions confirm", <span "Delete this snapshot?">;
            button("confirm-delete", "danger", "confirm-delete", null, "Delete", {});
            button("keep", "text-button", "keep", null, "Keep", {})>
        else <div class:"actions",
            <span id:"original", class:"original", r.url>
            button("rerun-all", "text-button", "rerun", "", "Re-run all", {});
            button("delete", "text-button", "delete", null, "Delete", {})>>
}

pub fn page(st, env, shown) => <main id:"archive", 'data-records':string(len(st.records)), 'data-jobs':string(len(st.jobs)),
    'data-loaded':string(st.loaded),
    <header class:"bars", (add_bar(st)); (search_bar(st))>
    <div class:"columns", (rail(st)); (middle(st, env)); (viewer(st, env, shown))>>
