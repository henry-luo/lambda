// Pure batch citation processing; callers capture resources before invoking it (S12.4.1v2).
import c: .common
import styles: .style
import locale: .locale
import e: .eval
import out: .output
import ordering: .sort

pub fn compile(style_xml, locale_xml, options = {}) map^ {
    let style = styles.compile(style_xml, c.get(options, "parents", {}), options)^
    let locales = locale.compile(locale_xml)^;
    {style: style, locales: locales, language: locale.language(c.get(options, "locale", style.default_locale))}
}

fn reference_for(references, id) {
    let found = [for (reference in references where reference.id == string(id)) reference]
    if (len(found) > 0) found[0] else null
}

fn layout(style, mode) => c.child(if (mode == "bibliography") style.bibliography else style.citation, "layout")

fn signatures(references, compiled) {
    let loc = locale.sources(compiled.style, compiled.locales, compiled.language);
    [for (ref in references)
        e.children(layout(compiled.style, "citation"),
            e.context(compiled.style, loc, ref, {}, c.attrs(compiled.style.citation))).text]
}

fn ambiguous(signatures, index) => any([for (i, value in signatures
    where i != index and value == signatures[index]) true])

fn suffix(index) {
    let alphabet = "abcdefghijklmnopqrstuvwxyz"
    if (index < 26) slice(alphabet, index, index + 1)
    else suffix(int(floor(index / 26)) - 1) ++ slice(alphabet, index % 26, index % 26 + 1)
}

fn add_names(references, compiled, count_value, limit) {
    if (not c.truth(compiled.style.citation["disambiguate-add-names"]) or count_value > limit) references
    else {
        let keys = signatures(references, compiled)
        let changed = [for (i, ref in references)
            if (ambiguous(keys, i)) {*:ref, name_count: count_value} else ref]
        if (all([for (i, key in keys) not ambiguous(keys, i)])) references
        else add_names(changed, compiled, count_value + 1, limit)
    }
}

fn primary(ref) => c.get(ref, "author", c.get(ref, "editor", []))[0]

fn add_given(references, compiled) {
    if (not c.truth(compiled.style.citation["disambiguate-add-givenname"])) references
    else {
        let rule = c.get(compiled.style.citation, "givenname-disambiguation-rule", "by-cite")
        let primary_only = starts_with(rule, "primary-name")
        let global = rule != "by-cite"
        let initials = contains(rule, "with-initials") or not global
        let keys = signatures(references, compiled)
        let chosen = [for (i, ref in references) {
            let person = primary(ref)
            let names_differ = any([for (other in references where other.id != ref.id and
                primary(other).family == person.family and primary(other).given != person.given) true])
            let collision = names_differ and (global or ambiguous(keys, i))
            if (collision and person.family != null) {*:ref, expand_given: true,
                given_initials: initials, given_primary_only: primary_only} else ref
        }]
        let initial_keys = signatures(chosen, compiled);
        [for (i, ref in chosen) if (ref.expand_given == true and ambiguous(initial_keys, i))
            {*:ref, given_initials: false} else ref]
    }
}

fn disambiguate(references, compiled) {
    let limit = max([1] ++ [for (ref in references) len(c.get(ref, "author", c.get(ref, "editor", [])))])
    let named = add_names(references, compiled, 2, limit)
    let given = add_given(named, compiled)
    let initial_keys = signatures(given, compiled)
    let branched = [for (i, ref in given)
        if (ambiguous(initial_keys, i)) {*:ref, disambiguate: true} else ref]
    let keys = signatures(branched, compiled);
    [for (i, reference in branched) {
        let prior = len([for (j, key in keys where j < i and key == keys[i]) key])
        if (c.truth(compiled.style.citation["disambiguate-add-year-suffix"]) and ambiguous(keys, i))
            {*:reference, year_suffix: suffix(prior), disambiguate: true}
        else reference
    }]
}

fn history(requests, index, processed, seen, previous, near_distance) {
    if (index >= len(requests)) processed
    else {
        let request = requests[index]
        let items = [for (item in request.items) {
            let prior = seen[string(item.id)]
            let note = c.as_int(request.note_index, 0)
            let same = len(request.items) == 1 and len(c.get(previous, "items", [])) == 1 and
                string(previous.items[0].id) == string(item.id) and
                (note == 0 or note == c.as_int(previous.note_index, 0) or
                    note == c.as_int(previous.note_index, 0) + 1)
            let position = if (prior == null) "first"
                else if (same and item.locator == previous.items[0].locator and
                    c.get(item, "label", "page") == c.get(previous.items[0], "label", "page")) "ibid"
                else if (same and (item.locator == null or previous.items[0].locator == null))
                    (if (item.locator == null) "ibid" else "ibid-with-locator")
                else if (same) "ibid-with-locator" else "subsequent";
            {*:item, position: position,
                'first-reference-note-number': c.get(prior, "first_note", note),
                'near-note': prior != null and note > 0 and prior.last_note > 0 and
                    note - prior.last_note <= near_distance}
        }]
        let next = c.dictionary([for (item in items) {key: string(item.id), value:
            {first_note: item["first-reference-note-number"], last_note: c.as_int(request.note_index, 0)}}])
        let prepared = {*:request, items: items}
        history(requests, index + 1, processed ++ [prepared], {*:seen, *:next}, prepared, near_distance)
    }
}

fn rendered_item(item, request, references, compiled, loc) {
    let reference = reference_for(references, item.id)
    if (reference == null) out.result([<span class: "latex-unsupported", "[?" ++ string(item.id) ++ "]">])
    else {
        let node = layout(compiled.style, if (request.mode == "full") "bibliography" else "citation")
        let context = e.context(compiled.style, loc, reference, item,
            c.attrs(compiled.style.citation))
        let body = if (request.mode == "author-only")
            e.render(<names variable: "author", <name form: "short">
                <substitute <names variable: "editor"> <text variable: "title">>>, context)
            else if (request.mode == "title-only") e.render(<text variable: "title">, context)
            else if (request.mode == "year-only")
                e.render(<date variable: "issued", <'date-part' name: "year">>, context)
            else e.children(node, if (request.mode == "suppress-author")
                {*:context, suppressed: ["author"]} else context)
        let completed = if (request.mode == "full") out.decorate(node, body, context) else body
        let linked = out.linked(completed, item.target)
        out.result([c.get(item, "prefix", ""), linked.content, c.get(item, "suffix", "")],
            body.attempted, body.successful, body.used)
    }
}

fn numeric_runs(items, index, runs, references) {
    if (index >= len(items)) runs
    else {
        let item = items[index]
        let ref = reference_for(references, item.id)
        let prior = if (len(runs) == 0) null else runs[len(runs) - 1]
        let simple = item.locator == null and c.get(item, "prefix", "") == "" and
            c.get(item, "suffix", "") == "" and ref != null
        let adjacent = simple and prior != null and prior.simple and
            ref.citation_number == prior.end_number + 1
        let run = if (adjacent) {*:prior, items: prior.items ++ [item],
            end_number: ref.citation_number} else
            {items: [item], simple: simple, end_number: ref.citation_number}
        numeric_runs(items, index + 1,
            (if (adjacent) slice(runs, 0, len(runs) - 1) else runs) ++ [run], references)
    }
}

fn rendered_cluster(request, references, compiled, loc) {
    let ordered_refs = ordering.references([for (item in request.items
        where reference_for(references, item.id) != null) reference_for(references, item.id)],
        compiled.style.citation, compiled.style, loc)
    let items = if (c.child(compiled.style.citation, "sort") == null) request.items else
        [for (reference in ordered_refs, item in request.items where string(item.id) == reference.id) item] ++
        [for (item in request.items where reference_for(references, item.id) == null) item]
    let node = layout(compiled.style, "citation")
    let context = {locales: loc}
    let collapse = compiled.style.citation.collapse
    let parts = if (request.mode == "textual")
        [for (item in items) {
            let author = rendered_item(item, {*:request, mode: "author-only"}, references, compiled, loc)
            let rest = rendered_item({*:item, prefix: ""}, {*:request, mode: "suppress-author"}, references, compiled, loc)
            out.combine([author, out.decorate(node, rest, context)], " ")
        }]
        else if (c.has(["year", "year-suffix", "year-suffix-ranged"], collapse) and
            c.get(request, "mode", "normal") == "normal")
        year_parts(items, 0, [], null, request, references, compiled, loc)
        else if (collapse == "citation-number" and c.get(request, "mode", "normal") == "normal")
        [for (run in numeric_runs(items, 0, [], references))
            if (len(run.items) >= 3) out.combine([
                rendered_item(run.items[0], request, references, compiled, loc),
                rendered_item(run.items[len(run.items) - 1], request, references, compiled, loc)], "–")
            else out.combine([for (item in run.items) rendered_item(item, request, references, compiled, loc)],
                c.get(node, "delimiter", ""))]
        else [for (item in items) rendered_item(item, request, references, compiled, loc)]
    let joined = out.combine(parts, c.get(node, "delimiter", ""))
    let combined = if (joined.text == "") {*:out.result([<span class: "latex-unsupported",
        "[CSL STYLE ERROR: reference with no printed form.]">]), empty: true} else joined
    if (c.has(["author-only", "title-only", "year-only", "full", "textual"], request.mode)) combined
    else out.decorate(node, combined, context)
}

fn year_parts(items, index, parts, previous, request, references, compiled, loc) {
    if (index >= len(items)) parts
    else {
        let item = items[index]
        let ref = reference_for(references, item.id)
        let signature = c.text(c.get(ref, "author", c.get(ref, "editor", [])))
        let same = previous != null and signature != "" and previous.signature == signature and
            item.locator == null and previous.item.locator == null and
            c.get(item, "prefix", "") == "" and c.get(previous.item, "suffix", "") == ""
        let rendered = rendered_item(item,
            if (same) {*:request, mode: "suppress-author"} else request, references, compiled, loc)
        let current = if (same) out.combine([parts[len(parts) - 1], rendered],
            c.get(compiled.style.citation, "cite-group-delimiter", ", ")) else rendered
        year_parts(items, index + 1,
            (if (same) slice(parts, 0, len(parts) - 1) else parts) ++ [current],
            {signature: signature, item: item}, request, references, compiled, loc)
    }
}

pub fn process(compiled, references, requests, options = {}) map^ {
    let ids = [for (reference in references) reference.id]
    if (len(unique(ids)) != len(ids))
        raise c.failure("duplicate-csl-id", "Reference identifiers must be unique")
    else {
        let cited = unique([for (request in requests, item in request.items) string(item.id)])
        let nocite = c.get(options, "nocite", [])
        let selected_ids = unique(cited ++ (if (c.has(nocite, "*")) ids else nocite))
        let selected = [for (i, id in selected_ids where reference_for(references, id) != null)
            {*:reference_for(references, id), citation_number: i + 1}]
        let loc = locale.sources(compiled.style, compiled.locales, compiled.language)
        let ordered = ordering.references(selected, compiled.style.bibliography, compiled.style, loc)
        let numbered = [for (i, reference in ordered) {*:reference, citation_number: i + 1}]
        let resolved = disambiguate(numbered, compiled)
        let prepared = history(requests, 0, [], {}, null,
            c.as_int(compiled.style.citation["near-note-distance"], 5))
        let citations = [for (request in prepared) {
            let rendered = out.finish(rendered_cluster(request, resolved, compiled, loc), loc);
            {id: request.id, content: rendered.content, text: rendered.text,
                note_index: request.note_index, items: request.items, empty: rendered.empty}
        }]
        let bibliography = if (compiled.style.bibliography == null) [] else
            [for (i, reference in resolved) {
                let substitute = compiled.style.bibliography["subsequent-author-substitute"]
                let people = c.get(reference, "author", c.get(reference, "editor", []))
                let prior_people = c.get(resolved[i - 1], "author", c.get(resolved[i - 1], "editor", []))
                let repeated = substitute != null and i > 0 and len(people) > 0 and people == prior_people
                let context = {*:e.context(compiled.style, loc, reference, {}, c.attrs(compiled.style.bibliography)),
                    author_substitute: if (repeated) substitute else null}
                let rendered = out.finish(e.render(layout(compiled.style, "bibliography"),
                    context), loc);
                {id: reference.id, content: rendered.content, text: rendered.text, reference: reference}
            }]
        let missing = [for (id in selected_ids where reference_for(references, id) == null)
            c.issue("unresolved-citation", "Unresolved citation " ++ id, null, id)]
        {citations: citations, bibliography: bibliography, references: resolved,
            diagnostics: missing ++ [for (citation in citations where citation.empty == true)
                c.issue("empty-csl-citation", "Citation style produced no printed form", null, citation.id)],
            style_id: compiled.style.id, locale: compiled.language}
    }
}
