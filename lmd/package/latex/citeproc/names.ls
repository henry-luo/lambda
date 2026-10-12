// CSL names are formatted from structured people, never parsed rendered strings.
import c: .common
import locale: .locale
import out: .output

fn capital_prefix(word, index = 0) {
    if (index >= len(word)) index
    else {
        let ch = slice(word, index, index + 1)
        if (ch != lower(ch) and ch == upper(ch)) capital_prefix(word, index + 1) else index
    }
}

fn initial(word) {
    let capitals = capital_prefix(word)
    if (capitals > 1 and capitals < len(word))
        upper(slice(word, 0, 1)) ++ lower(slice(word, 1, capitals))
    else upper(slice(word, 0, 1))
}

fn initials_word(word, initialize_with, hyphen) {
    let pieces = [for (i, piece in split(word, "-") where piece != "" and
        (i == 0 or slice(piece, 0, 1) != lower(slice(piece, 0, 1)))) piece]
    join([for (i, piece in pieces)
        initial(piece) ++ (if (i + 1 < len(pieces)) trim(initialize_with) else initialize_with)],
        if (hyphen) "-" else "")
}

fn initials(given, initialize_with, hyphen) =>
    join([for (i, word in c.words(replace(given, ".", " ")))
        if (i > 0 and word == lower(word) and len(word) > 1)
            (if (ends_with(initialize_with, " ")) "" else " ") ++ word ++ " "
        else initials_word(word, initialize_with, hyphen)], "")

fn particle_join(particle, family) => if (particle == "") family
    else particle ++ (if (ends_with(particle, "’") or ends_with(particle, "'")) "" else " ") ++ family

fn person_text(person, options, index, context) {
    let family_parts = [for (node in c.children(context.name_node, "name-part")
        where node["name"] == "family") node]
    let given_parts = [for (node in c.children(context.name_node, "name-part")
        where node["name"] == "given") node]
    if (person.literal != null)
        out.decorate(family_parts[0], out.result([person.literal]), context).content
    else {
        let family = c.get(person, "family", "")
        let dropping = c.get(person, "dropping-particle", "")
        let particle = c.get(person, "non-dropping-particle", "")
        let family_part = particle_join(particle, family)
        let expand_given = context.expand_given == true and
            (context.given_primary_only != true or index == 0)
        let short = c.get(options, "form", c.get(options, "name-form", "long")) == "short" and not expand_given
        let given_raw = c.get(person, "given", "")
        let given = if (short) "" else if (family == "" or (expand_given and context.given_initials != true) or
            c.get(options, "initialize", "true") == "false" or options["initialize-with"] == null)
            given_raw else trim(initials(given_raw, options["initialize-with"],
                c.get(context.style.root, "initialize-with-hyphen", "true") != "false"))
        let inverted = options["name-as-sort-order"] == "all" or
            (options["name-as-sort-order"] == "first" and index == 0) or context.sorting == true
        let given_part = given ++ (if (short or dropping == "" or not inverted) "" else
            (if (person['comma-dropping-particle'] == true) ", " else " ") ++ dropping)
        let demote = c.get(context.style.root, "demote-non-dropping-particle", "display-and-sort")
        let moved = inverted and particle != "" and
            (demote == "display-and-sort" or (demote == "sort-only" and context.sorting == true))
        let sorted_family = if (moved) family else family_part
        let sorted_given = given_part ++ (if (moved) " " ++ particle else "")
        let suffix = if (short) "" else c.get(person, "suffix", "")
        let suffix_sep = if (c.truth(person["comma-suffix"])) ", "
            else if (inverted) c.get(options, "sort-separator", ", ") else " "
        let displayed_family = sorted_family ++ (if (suffix == "" or inverted) "" else suffix_sep ++ suffix)
        // Dropping particles sit inside family affixes but retain their own case.
        let family_body = if (not inverted and not short and dropping != "")
            [<span class: "nocase", particle_join(dropping, "")>, displayed_family] else [displayed_family]
        let family_rich = out.decorate(family_parts[0], out.result(family_body), context).content
        let given_rich = out.decorate(given_parts[0], out.result([sorted_given]), context).content
        let east_asian = slice(family, 0, 1) >= "㐀" and slice(given_raw, 0, 1) >= "㐀"
        let base = if (east_asian) [family_rich, if (short) "" else given_raw]
            else if (inverted and sorted_given != "")
                [family_rich, c.get(options, "sort-separator", ", "), given_rich]
            else if (given_part == "") family_rich else [given_rich,
                if (ends_with(c.text(given_rich), " ") or family_part == "") "" else " ", family_rich];
        [base, if (suffix == "" or not inverted) "" else suffix_sep ++ suffix]
    }
}

fn joined(people, options, context, truncated, etal) {
    let delimiter = c.get(options, "delimiter", c.get(options, "name-delimiter", ", "))
    let last_rule = c.get(options, "delimiter-precedes-last", "contextual")
    let and_option = options["and"]
    let conjunction = if (and_option == "symbol") "&"
        else if (and_option == "text") locale.term(context.locales, "and") else null
    let person_count = len(people)
    let joined = [for (i, person in people) {
        if (i > 0) {
            if (i + 1 == person_count and conjunction != null and not truncated)
                (if (last_rule == "always" or (last_rule == "contextual" and person_count > 2))
                    delimiter else " ") ++ conjunction ++ " "
            else delimiter
        }
        person
    }]
    if (not truncated) joined
    else {
        let rule = c.get(options, "delimiter-precedes-et-al", "contextual")
        let sep = if (rule == "always" or (rule == "contextual" and person_count > 1)) delimiter else " ";
        [joined, sep, etal]
    }
}

pub fn render(node, people, context) {
    let name_node = c.child(node, "name")
    let options = {*:context.options, *:c.attrs(node), *:c.attrs(name_node)}
    let subsequent = context.position != null and context.position != "first"
    let min_count = c.as_int(if (subsequent) c.get(options, "et-al-subsequent-min", options["et-al-min"])
        else options["et-al-min"], 0)
    let first_count = max([1, c.as_int(if (subsequent)
        c.get(options, "et-al-subsequent-use-first", options["et-al-use-first"])
        else options["et-al-use-first"], 1), c.as_int(context.name_count, 0)])
    let forced = len(people) > 0 and people[len(people) - 1].literal == "others"
    let actual = if (forced) slice(people, 0, len(people) - 1) else people
    let truncated = forced or (min_count > 0 and len(actual) >= min_count and first_count < len(actual) and
        not c.truth(context.expand_names))
    let chosen = if (truncated) slice(actual, 0, first_count) else actual
    let named_context = {*:context, name_node: name_node}
    let formatted = [for (i, person in chosen) person_text(person, options, i, named_context)]
    let etal_node = c.child(node, "et-al")
    let etal = out.decorate(etal_node,
        out.result([locale.term(context.locales, "et-al")]), context).content
    let body = if (truncated and c.truth(options["et-al-use-last"]) and len(actual) > first_count + 1)
        [out.combine([for (person in formatted) out.result(person)], c.get(options, "delimiter", ", ")).content, ", … ",
            person_text(actual[len(actual) - 1], options, len(actual) - 1, named_context)]
        else joined(formatted, options, context, truncated, etal)
    out.decorate(name_node, out.result(if (c.get(options, "form", options["name-form"]) == "count")
        [string(len(chosen))] else body), context)
}
