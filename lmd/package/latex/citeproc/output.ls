// Rich fragments retain variable accounting until group suppression is decided.
import c: .common
import locale: .locale

fn rebuild(node, body) => if (name(node) == 'a') <a *:map(node), *body>
    else <span *:map(node), *body>

pub fn result(value = [], attempted = 0, successful = 0, used = []) =>
    {content: value, text: c.text(value), attempted: attempted,
        successful: successful, used: used}

pub fn literal(value) => result([{csl_literal: value}])

pub fn combine(parts, delimiter = "", suppress = false) {
    let attempted = sum([for (part in parts) part.attempted])
    let successful = sum([for (part in parts) part.successful])
    let shown = [for (part in parts where part.text != "") part]
    let body = if (suppress and attempted > 0 and successful == 0) [] else
        [for (i, part in shown) {
            if (i > 0) delimiter
            part.content
        }]
    result(body, attempted, successful, unique([for (part in parts, key in part.used) key]))
}

fn first_case(value, capital, index = 0) {
    if (index >= len(value)) value
    else {
        let ch = slice(value, index, index + 1)
        if (upper(ch) == lower(ch)) first_case(value, capital, index + 1)
        else slice(value, 0, index) ++ (if (capital) upper(ch) else lower(ch)) ++
            slice(value, index + 1, len(value))
    }
}

fn title_word(word, first, is_last) {
    let minor = ["a", "an", "and", "as", "at", "but", "by", "for", "from", "in",
        "into", "nor", "of", "on", "or", "over", "per", "the", "to", "via", "with"]
    if (contains(word, "-") or contains(word, "/") or contains(word, " ")) {
        let sep = if (contains(word, "-")) "-" else if (contains(word, "/")) "/" else " "
        let parts = split(word, sep)
        join([for (i, part in parts) title_word(part, first and i == 0,
            is_last and i + 1 == len(parts))], sep)
    } else if (slice(word, 0, 1) == lower(slice(word, 0, 1)) and word != lower(word)) word
    else if (not first and not is_last and c.has(minor, lower(word))) lower(word)
    else first_case(word, true)
}

fn case_text(value, mode) {
    if (mode == "uppercase") upper(value)
    else if (mode == "lowercase") lower(value)
    else if (mode == "capitalize-first") first_case(value, true)
    else if (mode == "sentence") first_case(lower(value), true)
    else if (mode == "capitalize-all" or mode == "title") {
        let words = split(value, " ");
        join([for (i, word in words)
            if (mode == "title") title_word(word, i == 0 or ends_with(words[i - 1], ":"), i + 1 == len(words))
            else first_case(word, true)], " ")
    } else value
}

fn transform(value, mode, strip) {
    if (mode == "sentence" or mode == "capitalize-first")
        capitalize_tree(transform(value, if (mode == "sentence") "lowercase" else "", strip), false).value
    else if (value is string)
        case_text(if (strip) replace(value, ".", "") else value, mode)
    else if (value is element) {
        if (value.class == "nocase" or value.class == "nodecor") value
        else {
            let children = [for (child in content(value)) transform(child, mode, strip)]
            rebuild(value, children)
        }
    } else if (value is array or value is list)
        [for (child in value) transform(child, mode, strip)]
    else if (value is map and value.csl_literal != null)
        {csl_literal: transform(value.csl_literal, mode, strip)}
    else value
}

fn capitalize_parts(parts, index, values, done) {
    if (index >= len(parts)) {value: values, done: done}
    else {
        let current = capitalize_tree(parts[index], done)
        capitalize_parts(parts, index + 1, values ++ [current.value], current.done)
    }
}

fn capitalize_tree(value, done) {
    if (value is string) {value: if (done) value else first_case(value, true),
        done: done or lower(value) != upper(value)}
    else if (value is element) {
        if (value.class == "nocase") {value: value, done: done or lower(c.text(value)) != upper(c.text(value))}
        else {
            let result = capitalize_parts(content(value), 0, [], done);
            {value: rebuild(value, result.value), done: result.done}
        }
    } else if (value is array or value is list) capitalize_parts(value, 0, [], done)
    else if (value is map and value.csl_literal != null) {
        let result = capitalize_tree(value.csl_literal, done);
        {value: {csl_literal: result.value}, done: result.done}
    } else {value: value, done: done}
}

fn css(node) {
    let props = ["font-style", "font-weight", "font-variant", "text-decoration", "vertical-align"]
    join([for (key in props where node[key] != null) key ++ ":" ++
        (if (key == "vertical-align" and node[key] == "sup") "super" else node[key])], ";")
}

pub fn decorate(node, value, context) {
    if (value.text == "") value
    else {
        let requested = c.get(node, "text-case", "")
        let lang = context.reference.language
        let mode = if (requested == "title" and lang != null and
            not starts_with(lower(lang), "en")) "" else requested
        let body = transform(value.content, mode, c.truth(node["strip-periods"]))
        let quoted = if (c.truth(node.quotes))
            [locale.term(context.locales, "open-quote"), body,
                locale.term(context.locales, "close-quote")] else body
        let style = css(node)
        let decorated = if (style == "") quoted else <span style: style, quoted>
        let display = node.display
        let block = if (display == null) decorated
            else <span class: "csl-" ++ display,
                if (display == "right-inline") " "
                decorated>
        {*:value, content: [c.get(node, "prefix", ""), block, c.get(node, "suffix", "")],
            text: c.get(node, "prefix", "") ++ c.text(block) ++ c.get(node, "suffix", "")}
    }
}

fn has_link(value) => if (value is element) name(value) == 'a' or has_link(content(value))
    else if (value is array or value is list) any([for (child in value) has_link(child)]) else false

pub fn linked(value, target) => if (target == null or target == "" or has_link(value.content)) value
    else {*:value, content: [<a class: "latex-cite", href: target, value.content>]}

pub fn safe_url(value) bool {
    if (not (value is string)) false
    else (starts_with(lower(value), "https://") ^ { false }) or
        (starts_with(lower(value), "http://") ^ { false }) or
        (starts_with(lower(value), "mailto:") ^ { false })
}

fn edge(value, front, replacement) {
    if (value is string) {
        if (len(value) == 0) value
        else if (front) replacement ++ slice(value, 1, len(value))
        else slice(value, 0, len(value) - 1) ++ replacement
    } else if (value is element) rebuild(value, edge(content(value), front, replacement))
    else if (value is array or value is list) {
        let visible = [for (i, item in value where c.text(item) != "") i]
        let chosen = if (front) visible[0] else visible[len(visible) - 1];
        [for (i, item in value) if (i == chosen) edge(item, front, replacement) else item]
    } else value
}

fn flatten(value) => if (value is array or value is list)
    [for (item in value, child in flatten(item)) child]
    else if (value == null or value == "") [] else [value]

fn smooth(parts, index, result, quotes, punctuation) {
    if (index >= len(parts)) result
    else {
        let part = parts[index]
        let prior = if (len(result) == 0) null else result[len(result) - 1]
        let before = c.text(prior)
        let after = c.text(part)
        let tail = slice(before, max([0, len(before) - 1]), len(before))
        let head = slice(after, 0, 1)
        let moved = punctuation and c.has(quotes, tail) and c.has([".", ","], head) and
            not (part is map) and not (prior is map)
        let duplicate = (tail == head and c.has([".", ",", ":", ";", "!", "?"], head)) or
            (head == "." and c.has(["!", "?"], tail))
        let updated = if (moved) slice(result, 0, len(result) - 1) ++
            [edge(prior, false, head ++ tail)] else result
        let current = if (moved or duplicate) edge(part, true, "") else part
        smooth(parts, index + 1, updated ++ (if (c.text(current) == "") [] else [current]), quotes, punctuation)
    }
}

fn clean(value, quotes, punctuation) {
    let parts = [for (part in flatten(value))
        if (part is element) rebuild(part, clean(content(part), quotes, punctuation)) else part]
    smooth(parts, 0, [], quotes, punctuation)
}

fn unwrap(value) => if (value is map and value.csl_literal != null) value.csl_literal
    else if (value is element) rebuild(value, unwrap(content(value)))
    else if (value is array or value is list) [for (part in value) unwrap(part)] else value

pub fn finish(value, locales) {
    let body = clean(value.content,
        [locale.term(locales, "close-quote"), locale.term(locales, "close-inner-quote")],
        c.truth(locale.option(locales, "punctuation-in-quote", "false")))
    {*:value, content: unwrap(body), text: c.text(body)}
}
