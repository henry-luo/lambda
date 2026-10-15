// The CSL rendering tree is interpreted directly by Lambda functions (D7.2.3, S1.8).
import c: .common
import out: .output
import names: .names
import dates: .dates
import numbers: .numbers
import locale: .locale
import ranges: .ranges

pub fn context(style, locales, reference, item = {}, options = {}) =>
    {style: style, locales: locales, reference: reference, item: item,
        options: {*:c.attrs(style.root), *:options}, suppressed: [],
        position: c.get(item, "position", "first"),
        year_suffix: if (style.explicit_year_suffix == true) "" else c.get(reference, "year_suffix", ""),
        expand_names: c.get(reference, "expand_names", false),
        expand_given: c.get(reference, "expand_given", false),
        given_initials: c.get(reference, "given_initials", false),
        given_primary_only: c.get(reference, "given_primary_only", false),
        name_count: c.get(reference, "name_count", 0),
        disambiguate: c.get(reference, "disambiguate", false)}

pub fn variable(key, context, form = "long") {
    let ref = context.reference
    if (c.has(context.suppressed, key)) null
    else if (key == "locator") context.item.locator
    else if (key == "citation-number") ref.citation_number
    else if (key == "year-suffix") ref.year_suffix
    else if (key == "editortranslator") ref.editor
    else if (key == "first-reference-note-number") context.item["first-reference-note-number"]
    else if (key == "citation-label") {
        let people = c.get(ref, "author", c.get(ref, "editor", []))
        let family = if (len(people) == 0) c.get(ref, "title", ref.id)
            else c.get(people[0], "family", people[0].literal)
        slice(c.text(family), 0, 3) ++ c.text(ref.issued["date-parts"][0][0])
    } else if (form == "short" and ref[key ++ "-short"] != null) ref[key ++ "-short"]
    else ref[key]
}

fn plural(value) {
    if (value is array or value is list) len(value) > 1
    else contains(c.text(value), "–") or contains(c.text(value), "-") or
        contains(c.text(value), ",") or contains(c.text(value), "&")
}

type NumericToken = \("\a"* "\d"+ "\a"*)

pub fn numeric(value) {
    let text = trim(c.text(value))
    let tokens = c.words(replace(replace(replace(replace(text, "–", " "), "-", " "), ",", " "), "&", " "))
    len(tokens) > 0 and all([for (token in tokens) token is NumericToken])
}

fn condition(node, context) {
    let tests = (if (node.type == null) [] else
        [for (key in c.words(node.type)) context.reference.type == key]) ++
        (if (node.variable == null) [] else
        [for (key in c.words(node.variable)) c.nonempty(variable(key, context))]) ++
        (if (node["is-numeric"] == null) [] else
        [for (key in c.words(node["is-numeric"])) numeric(variable(key, context))]) ++
        (if (node["is-uncertain-date"] == null) [] else
        [for (key in c.words(node["is-uncertain-date"])) c.truth(variable(key, context).circa)]) ++
        (if (node.locator == null) [] else
        [for (key in c.words(node.locator)) c.get(context.item, "label", "page") == key and
            c.nonempty(context.item.locator)]) ++
        (if (node.position == null) [] else
        [for (key in c.words(node.position))
            key == context.position or (key == "ibid" and context.position == "ibid-with-locator") or
                (key == "subsequent" and context.position != "first") or
                (key == "near-note" and c.truth(context.item["near-note"]))]) ++
        (if (node.disambiguate == null) [] else
            [c.truth(node.disambiguate) == c.truth(context.disambiguate)])
    let match_rule = c.get(node, "match", "all")
    if (match_rule == "any") any(tests)
    else if (match_rule == "none") not any(tests) else all(tests)
}

fn choose(branches, index, context) {
    if (index >= len(branches)) out.result()
    else if (c.tag(branches[index]) == "else" or condition(branches[index], context))
        children(branches[index], context)
    else choose(branches, index + 1, context)
}

fn selected_branch(branches, index, context) {
    if (index >= len(branches)) []
    else if (c.tag(branches[index]) == "else" or condition(branches[index], context))
        c.children(branches[index])
    else selected_branch(branches, index + 1, context)
}

fn expanded(nodes, context) => [for (node in nodes, child in
    (if (c.tag(node) == "choose") expanded(selected_branch(c.children(node), 0, context), context)
        else [node])) child]

fn variable_text(node, context) {
    let key = node.variable
    let form = c.get(node, "form", "long")
    let value = variable(key, context, form)
    let rich_key = if (form == "short" and context.reference[key ++ "-short"] != null)
        key ++ "-short" else key
    let rich = context.reference.rich[rich_key]
    let page = key == "page" or (key == "locator" and c.get(context.item, "label", "page") == "page")
    let delimiter = locale.term(context.locales, "page-range-delimiter")
    let body = if (value == null) [] else if (page)
        [ranges.render(value, context.style.root["page-range-format"], if (delimiter == "") "–" else delimiter)]
        else if (rich != null and not c.truth(context.sorting)) rich
        else [c.text(value)]
    let linked = if (key == "URL" and out.safe_url(value)) <a href: value, body>
        else if (key == "DOI" and c.nonempty(value))
            <a href: (if (out.safe_url(value)) value else "https://doi.org/" ++ value), body>
        else body
    out.result(linked, 1, if (c.nonempty(value)) 1 else 0, if (c.nonempty(value)) [key] else [])
}

fn text_node(node, context) {
    if (node.macro != null) children(context.style.macros[node.macro], context)
    else if (node.variable != null) variable_text(node, context)
    else if (node.term != null) out.result([locale.term(context.locales, node.term,
        c.get(node, "form", "long"), c.truth(node.plural))])
    else out.literal(c.get(node, "value", ""))
}

fn label_node(node, context, override = null) {
    let key = c.get(node, "variable", override)
    let value = variable(key, context)
    let term_name = if (key == "locator") c.get(context.item, "label", "page") else key
    let is_plural = if (node.plural == "always") true
        else if (node.plural == "never") false else plural(value)
    out.result(if (not c.nonempty(value)) [] else
        [locale.term(context.locales, term_name, c.get(node, "form", "long"), is_plural)],
        1, if (c.nonempty(value)) 1 else 0, if (c.nonempty(value)) [key] else [])
}

fn substitute(nodes, index, context) {
    if (index >= len(nodes)) out.result([], 1, 0)
    else {
        let rendered = render(nodes[index], context)
        if (rendered.text == "") substitute(nodes, index + 1, context)
        else {*:rendered, suppressed: rendered.used}
    }
}

fn names_node(node, context) {
    let keys = c.words(node.variable)
    let merge_roles = c.has(keys, "editor") and c.has(keys, "translator") and
        context.reference.editor == context.reference.translator and
        locale.term(context.locales, "editortranslator") != ""
    let values = [for (key in keys where c.nonempty(variable(key, context)) and
        not (merge_roles and key == "translator"))
        {key: if (merge_roles and key == "editor") "editortranslator" else key, people: variable(key, context)}]
    if (len(values) > 0 and context.author_substitute != null and
        c.has(["author", "editor"], values[0].key))
        out.result([context.author_substitute], 1, 1, [values[0].key])
    else if (len(values) == 0) {
        let inherited_name = c.child(node, "name")
        let options = {*:context.options, *:c.attrs(node), *:c.attrs(inherited_name)}
        substitute(c.children(c.child(node, "substitute")), 0, {*:context, options: options})
    } else {
        let label = c.child(node, "label")
        let parts = [for (part in values) {
            let rendered = names.render(node, part.people, context)
            let labeled = if (label == null) rendered else {
                let label_result = out.decorate(label, label_node(label, context, part.key), context)
                if (c.tag(c.children(node)[0]) == "label")
                    out.combine([label_result, rendered]) else out.combine([rendered, label_result])
            }
            {*:labeled, attempted: 1, successful: 1, used: [part.key]}
        }]
        if (c.get(c.child(node, "name"), "form", context.options["name-form"]) == "count")
            out.result([string(sum([for (part in parts) c.as_int(part.text)]))], len(values), len(values),
                [for (part in values) part.key])
        else out.combine(parts, c.get(node, "delimiter", c.get(context.options, "names-delimiter", "")))
    }
}

fn sequence(nodes, index, context, parts, suppressed) {
    if (index >= len(nodes)) {parts: parts, suppressed: suppressed}
    else {
        let rendered = render(nodes[index], {*:context, suppressed: context.suppressed ++ suppressed})
        sequence(nodes, index + 1, context, parts ++ [rendered],
            unique(suppressed ++ c.get(rendered, "suppressed", [])))
    }
}

pub fn children(node, context) {
    let result = sequence(expanded(c.children(node), context), 0, context, [], [])
    let combined = if (c.tag(node) == "layout" and context.options["second-field-align"] != null and
        len(result.parts) > 1)
        out.result([<span class: "csl-left-margin", result.parts[0].content>,
            <span class: "csl-right-inline", " "
                out.combine(slice(result.parts, 1, len(result.parts))).content>])
        else out.combine(result.parts, c.get(node, "delimiter", ""), c.tag(node) == "group")
    {*:combined, suppressed: result.suppressed}
}

pub fn render(node, context) {
    let tag = c.tag(node)
    let value = if (tag == "text") text_node(node, context)
        else if (tag == "number") {
            let value = variable(node.variable, context)
            out.result(if (value == null) [] else
                [numbers.render(value, c.get(node, "form", "numeric"), context.locales,
                    locale.gender(context.locales, node.variable))],
                1, if (c.nonempty(value)) 1 else 0, if (c.nonempty(value)) [node.variable] else [])
        } else if (tag == "label") label_node(node, context)
        else if (tag == "names") names_node(node, context)
        else if (tag == "date") {
            let rendered = dates.render(node, variable(node.variable, context),
                if (node.variable == "issued") context else {*:context, year_suffix: ""})
            {*:rendered, used: if (rendered.text == "") [] else [node.variable]}
        } else if (tag == "choose") choose(c.children(node), 0, context)
        else if (tag == "group" or tag == "layout" or tag == "macro") children(node, context)
        else out.result()
    out.decorate(node, value, context)
}
