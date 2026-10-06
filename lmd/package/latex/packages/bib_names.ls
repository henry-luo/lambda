// Shared BibLaTeX name parsing and display, kept separate from citation styles.
import util: ~~.util

fn split_names_at(source, index, start, braces, escaped, names) {
    if (index >= len(source)) names ++ [trim(slice(source, start, len(source)))]
    else {
        let ch = slice(source, index, index + 1)
        if (escaped) split_names_at(source, index + 1, start, braces, false, names)
        else if (ch == "\\") split_names_at(source, index + 1, start, braces, true, names)
        else if (ch == "{") split_names_at(source, index + 1, start, braces + 1, false, names)
        else if (ch == "}") split_names_at(source, index + 1, start, braces - 1, false, names)
        else if (braces == 0 and slice(source, index, index + 5) == " and ")
            split_names_at(source, index + 5, index + 5, braces, false,
                names ++ [trim(slice(source, start, index))])
        else split_names_at(source, index + 1, start, braces, false, names)
    }
}

fn plain(source) => replace(replace(trim(source), "{", ""), "}", "")

fn word_list(source) => [for (word in split(trim(source), " ") where word != "") word]

fn join_words(words, from, count) {
    if (from >= count) ""
    else if (from + 1 >= count) words[from]
    else words[from] ++ " " ++ join_words(words, from + 1, count)
}

fn lower_word(word) {
    let first = slice(word, 0, 1)
    first != "" and first == lower(first)
}

fn prefix_start(words, index, end) {
    if (index >= end) null
    else if (lower_word(words[index])) index
    else prefix_start(words, index + 1, end)
}

fn family_parts(source) {
    let words = word_list(plain(source))
    let boundary = prefix_start(words, 0, len(words) - 1)
    if (boundary == null) {prefix: "", family: plain(source)}
    else {
        let after = prefix_end(words, boundary, len(words) - 1)
        {prefix: join_words(words, boundary, after),
            family: join_words(words, after, len(words))}
    }
}

fn prefix_end(words, index, final_index) {
    if (index >= final_index or not lower_word(words[index])) index
    else prefix_end(words, index + 1, final_index)
}

fn parse_single(source) {
    let raw = trim(source)
    let comma = util.split_top_level(raw, ",")
    if (starts_with(raw, "{") and ends_with(raw, "}") and len(comma) == 1) {
        let literal = plain(raw)
        {family: literal, given: "", prefix: "", suffix: "",
            literal: literal, raw: raw}
    } else if (len(comma) >= 2) {
        let family_name = family_parts(comma[0])
        let suffix = if (len(comma) >= 3) plain(comma[1]) else ""
        let given = plain(comma[len(comma) - 1])
        {family: family_name.family, given: given, prefix: family_name.prefix, suffix: suffix,
            literal: null, raw: raw}
    } else {
        let words = word_list(plain(raw))
        let count = len(words)
        let boundary = prefix_start(words, 1, count - 1)
        let split_at = if (boundary == null) count - 1 else boundary
        let after = if (boundary == null) count - 1
            else prefix_end(words, boundary, count - 1)
        let family = join_words(words, after, count)
        let given = join_words(words, 0, split_at)
        let prefix = if (boundary == null) "" else join_words(words, boundary, after)
        {family: family, given: given, prefix: prefix, suffix: "",
            literal: null, raw: raw}
    }
}

pub fn parse(source) {
    if (source == null or trim(source) == "") []
    else [for (person in split_names_at(source, 0, 0, 0, false, [])
        where person != "") parse_single(person)]
}

pub fn primary(entry) {
    let fields = if (entry.raw_fields != null) entry.raw_fields else entry.fields
    let raw = if (fields.author != null) fields.author
        else if (fields.editor != null) fields.editor else ""
    parse(raw)
}

fn initial_words(words, index, acc) {
    if (index >= len(words)) acc
    else {
        let word = words[index]
        initial_words(words, index + 1,
            acc ++ (if (word == "") "" else slice(word, 0, 1) ++ "."))
    }
}

pub fn given_initials(person) {
    initial_words(word_list(person.given), 0, "")
}

pub fn display_person(person, family_first, giveninits) {
    if (person.literal != null) person.literal
    else {
        let given = if (giveninits) given_initials(person) else person.given
        let suffix = if (person.suffix == "") "" else ", " ++ person.suffix
        let family = (if (person.prefix == "") "" else person.prefix ++ " ") ++ person.family
        if (given == "") family ++ suffix
        else if (family_first) family ++ ", " ++ given ++ suffix
        else given ++ " " ++ family ++ suffix
    }
}

fn display_person_mode(person, mode, family_first, giveninits) {
    if (mode == "family")
        (if (person.prefix == "") "" else person.prefix ++ " ") ++ person.family
    else if (mode == "init" or mode == "full")
        display_person(person, false, mode == "init")
    else display_person(person, family_first, giveninits)
}

fn display_list(names, index, count, family_first, giveninits, and_word, mode) {
    if (index >= count) ""
    else {
        let person = display_person_mode(names[index], mode, family_first, giveninits)
        if (index + 1 >= count) person
        else if (index + 2 >= count) person ++ " " ++ and_word ++ " " ++
            display_list(names, index + 1, count, family_first, giveninits, and_word, mode)
        else person ++ ", " ++
            display_list(names, index + 1, count, family_first, giveninits, and_word, mode)
    }
}

fn display_mode(names, maxnames, minnames, family_first, giveninits, and_word, etal, mode) {
    let count = len(names)
    let shown = if (maxnames != null and count > maxnames)
        (if (minnames != null) minnames else maxnames) else count
    let limit = if (shown < 0) 0 else if (shown > count) count else shown
    display_list(names, 0, limit, family_first, giveninits, and_word, mode) ++
        (if (limit < count) (if (limit > 0) " " else "") ++ etal else "")
}

pub fn display(names, maxnames, minnames, family_first, giveninits, and_word, etal) =>
    display_mode(names, maxnames, minnames, family_first, giveninits, and_word, etal, "normal")

pub fn citation(names, maxnames, minnames, and_word, etal, mode) =>
    display_mode(names, maxnames, minnames, false, false, and_word, etal, mode)

pub fn short(names, and_word, etal) {
    if (len(names) == 0) ""
    else if (len(names) == 1) names[0].family
    else if (len(names) == 2) names[0].family ++ " " ++ and_word ++ " " ++ names[1].family
    else names[0].family ++ " " ++ etal
}

pub fn sort_name(names) {
    if (len(names) == 0) ""
    else join([for (person in names)
        lower(person.family ++ ", " ++ person.prefix ++ " " ++
            person.given ++ " " ++ person.suffix)], "|")
}

pub fn family_key(names) => join([for (person in names)
    lower(person.prefix ++ " " ++ person.family)], "|")

pub fn initial_key(names) => join([for (person in names)
    lower(given_initials(person))], "|")
