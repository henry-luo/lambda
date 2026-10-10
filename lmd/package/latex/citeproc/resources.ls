// This is the only citeproc I/O seam; the processor receives captured immutable values.
import c: .common
import cp: .citeproc
import data: .data
import bib: ~~.packages.bib_data
import locale: .locale

fn captured(names, index, base, inline, macros, entries, json_refs, issues, assets) {
    if (index >= len(names)) {entries: entries, references: json_refs, diagnostics: issues, assets: assets}
    else {
        let resource = names[index]
        let local = bib.inline_source(inline, resource.source)
        let path = bib.resource_path(resource.source, base)
        let source = if (local != null) local.source else input(path, "text") ^ { null }
        let file = if (local != null) "filecontents:" ++ resource.source else path
        let asset = {kind: "bibliography", source: file, origin: if (local == null) "local-file" else "inline",
            available: source != null, offset: resource.offset}
        if (source == null) captured(names, index + 1, base, inline, macros, entries, json_refs,
            issues ++ [c.issue("missing-bib-resource", "Cannot read bibliography resource " ++ path, file)], assets ++ [asset])
        else if (ends_with(lower(resource.source), ".json")) {
            let parsed = data.json(source, file) ^ {
                {references: [], diagnostics: [c.error_issue(^, file)]}
            }
            captured(names, index + 1, base, inline, macros, entries, json_refs ++ parsed.references,
                issues ++ parsed.diagnostics, assets ++ [asset])
        } else {
            let parsed = bib.parse_source(source, file, macros, [for (key, value in data.TYPES) string(key)])
            captured(names, index + 1, base, inline, parsed.macros, entries ++ parsed.entries,
                json_refs, issues ++ parsed.diagnostics, assets ++ [asset])
        }
    }
}

fn style_capture(options, base) {
    if (options.style_xml != null) {xml: options.style_xml, source: "<inline-style>", assets: [], diagnostics: []}
    else {
        let requested = c.get(options, "style", "ieee")
        let bundled = c.has(["ieee", "apa", "chicago-author-date", "chicago-fullnote-bibliography"], requested)
        let path = if (bundled) sys.lambda.home# ++ "/package/latex/citeproc/styles/" ++ requested ++ ".csl"
            else bib.resource_path(requested, base)
        let captured = xml_capture(path, null, "csl-style");
        {*:captured, source: path}
    }
}

// Overrides and dependent parents use the same local capture contract as styles.
fn xml_capture(value, base, kind) {
    if (starts_with(trim(value), "<")) {xml: value, assets: [], diagnostics: []}
    else {
        let path = bib.resource_path(value, base)
        let xml = input(path, "text") ^ { null };
        {xml: xml, assets: [{kind: kind, source: path, available: xml != null}],
            diagnostics: if (xml != null) [] else
                [c.issue("missing-csl-resource", "Cannot read " ++ kind ++ " resource " ++ path, path)]}
    }
}

pub fn load(ast, base, options, language) {
    let loaded = captured(bib.resources(ast), 0, base, bib.inline_resources(ast), [], [], [], [], [])
    let bib_refs = data.normalize_entries(bib.finalize(loaded.entries, loaded.diagnostics))
    let all_refs = bib_refs.references ++ loaded.references
    let refs = [for (i, ref in all_refs where not any([for (j, prior in all_refs
        where j < i and prior.id == ref.id) true])) ref]
    let duplicates = [for (i, ref in all_refs where any([for (j, prior in all_refs
        where j < i and prior.id == ref.id) true]))
        c.issue("duplicate-csl-id", "Duplicate reference identifier " ++ ref.id, ref.provenance.resource, ref.id)]
    let capture = compile_options(options, base, language)
    {compiled: capture.compiled, references: refs, entries: bib_refs.entries,
        diagnostics: bib_refs.diagnostics ++ duplicates ++ capture.diagnostics,
        assets: loaded.assets ++ capture.assets}
}

fn compile_options(options, base, language) {
    let style = style_capture(options, base)
    let locale_inputs = c.get(options, "locales", {})
    let bundled_captures = [for (lang in ["en-US", "de-DE", "fr-FR"])
        {key: lang, captured: xml_capture(sys.lambda.home# ++ "/package/latex/citeproc/locales/locales-" ++ lang ++ ".xml",
            null, "csl-locale")}]
    let bundled = c.dictionary([for (item in bundled_captures) {key: item.key, value: item.captured.xml}])
    let locale_captures = [for (lang, value in locale_inputs)
        {key: locale.language(string(lang)), captured: xml_capture(value, base, "csl-locale")}]
    let parent_captures = [for (id, value in c.get(options, "parents", {}))
        {key: string(id), captured: xml_capture(value, base, "csl-parent")}]
    let overrides = c.dictionary([for (item in locale_captures) {key: item.key, value: item.captured.xml}])
    let parents = c.dictionary([for (item in parent_captures) {key: item.key, value: item.captured.xml}])
    let selected = locale.language(c.get(options, "locale", language))
    let captures = bundled_captures ++ locale_captures ++ parent_captures
    let issues = style.diagnostics ++ [for (item in captures, issue in item.captured.diagnostics) issue]
    let attempt = if (len(issues) > 0) {failure: null} else cp.compile(style.xml, {*:bundled, *:overrides},
        {*:options, parents: parents, source: style.source, locale: selected}) ^ { {failure: c.error_issue(^)} }
    let compiled = if (len(issues) > 0 or attempt.failure != null) null else attempt;
    {compiled: compiled, assets: style.assets ++ [for (item in captures, asset in item.captured.assets) asset],
        diagnostics: issues ++ (if (attempt.failure == null) [] else [attempt.failure]) ++
            (if (compiled == null or compiled.locales[selected] != null) [] else
                [c.issue("csl-locale-fallback", "Locale " ++ selected ++ " falls back to bundled terms")])}
}
