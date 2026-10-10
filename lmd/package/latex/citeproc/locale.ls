// Locale fallback is per unit, including intentionally empty term definitions.
import c: .common

pub fn language(value) {
    let source = c.get({value: value}, "value", "en-US")
    let aliases = {english: "en-US", en: "en-US", german: "de-DE", de: "de-DE",
        french: "fr-FR", fr: "fr-FR"}
    c.get(aliases, source, replace(source, "_", "-"))
}

pub fn compile(values) map^ {
    let compiled = [for (key, value in values) {
        let root = c.root(if (value is string) parse(value, "xml")^ else value)
        if (c.tag(root) != "locale" or root.version != "1.0" or c.namespace_uri(root) != c.NS)
            raise c.failure("invalid-csl-locale", "Expected a CSL locale", string(key))
        else {key: string(key), root: root}
    }]
    c.dictionary([for (item in compiled) {key: item.key, value: item.root}])
}

pub fn sources(style, locales, lang) {
    let chosen = language(c.get({lang: lang}, "lang", style.default_locale))
    let base = split(chosen, "-")[0]
    let primary = language(base);
    [for (node in style.locales where node["xml:lang"] == chosen) node] ++
    [for (node in style.locales where node["xml:lang"] == base) node] ++
    [for (node in style.locales where node["xml:lang"] == null) node] ++
    [for (key in unique([chosen, primary, "en-US"]) where locales[key] != null) locales[key]]
}

fn lookup(sources, term_name, form, plural, index) {
    if (index >= len(sources)) null
    else {
        let terms = c.children(c.child(sources[index], "terms"), "term")
        let found = [for (term in terms where term["name"] == term_name and
            c.get(term, "form", "long") == form) term]
        if (len(found) == 0) lookup(sources, term_name, form, plural, index + 1)
        else {
            let node = found[0]
            let variant = c.child(node, if (plural) "multiple" else "single")
            c.text(if (variant != null) variant else node)
        }
    }
}

pub fn term(sources, term_name, form = "long", plural = false) {
    let found = lookup(sources, term_name, form, plural, 0)
    if (found != null) found
    else if (form == "verb-short") term(sources, term_name, "verb", plural)
    else if (form == "symbol") term(sources, term_name, "short", plural)
    else if (form != "long") term(sources, term_name, "long", plural)
    else ""
}

pub fn date_format(sources, form, index = 0) {
    if (index >= len(sources)) null
    else {
        let found = [for (node in c.children(sources[index], "date") where node.form == form) node]
        if (len(found) > 0) found[0] else date_format(sources, form, index + 1)
    }
}

pub fn option(sources, term_name, fallback = null, index = 0) {
    if (index >= len(sources)) fallback
    else {
        let value = c.child(sources[index], "style-options")[term_name]
        if (value != null) value else option(sources, term_name, fallback, index + 1)
    }
}

pub fn ordinal_terms(sources, index = 0) {
    if (index >= len(sources)) []
    else {
        let found = [for (term in c.children(c.child(sources[index], "terms"), "term")
            where term["name"] == "ordinal" or starts_with(term["name"], "ordinal-")) term]
        if (len(found) > 0) found else ordinal_terms(sources, index + 1)
    }
}

pub fn gender(sources, term_name, index = 0) {
    if (index >= len(sources)) null
    else {
        let found = [for (term in c.children(c.child(sources[index], "terms"), "term")
            where term["name"] == term_name and c.get(term, "form", "long") == "long") term]
        if (len(found) > 0) found[0].gender else gender(sources, term_name, index + 1)
    }
}
