// Shared sorting, labels and built-in presentation profiles for biblatex.
import names: .bib_names

pub let STYLES = ["numeric", "numeric-comp", "authoryear", "authoryear-comp",
    "alphabetic", "authortitle"]
pub let SORTING = ["none", "nty", "nyt", "ynt"]

pub fn supported_style(value) => any([for (style in STYLES) style == value])
pub fn supported_sorting(value) => any([for (sorting in SORTING) sorting == value])
pub fn supported_language(value) => value == "english" or value == "en" or
    value == "german" or value == "ngerman" or value == "de" or
    value == "french" or value == "fr"

pub fn settings(opts, language) {
    let chosen = if (opts == null or opts.style == null) "numeric" else opts.style
    {style: chosen,
     citestyle: if (opts != null and opts.citestyle != null) opts.citestyle else chosen,
     bibstyle: if (opts != null and opts.bibstyle != null) opts.bibstyle else chosen,
     sorting: if (opts != null and opts.sorting != null) opts.sorting else "nty",
     maxcitenames: if (opts != null and opts.maxcitenames != null) int(opts.maxcitenames) ^ { 2 }
         else if (opts != null and opts.maxnames != null) int(opts.maxnames) ^ { 2 } else 2,
     mincitenames: if (opts != null and opts.mincitenames != null) int(opts.mincitenames) ^ { 1 }
         else if (opts != null and opts.minnames != null) int(opts.minnames) ^ { 1 } else 1,
     maxbibnames: if (opts != null and opts.maxbibnames != null) int(opts.maxbibnames) ^ { 99 }
         else if (opts != null and opts.maxnames != null) int(opts.maxnames) ^ { 99 } else 99,
     minbibnames: if (opts != null and opts.minbibnames != null) int(opts.minbibnames) ^ { 1 }
         else if (opts != null and opts.minnames != null) int(opts.minnames) ^ { 1 } else 1,
     giveninits: opts != null and opts.giveninits == "true",
     uniquename: if (opts != null and opts.uniquename != null) opts.uniquename else "init",
     natbib: opts != null and opts.natbib == "true",
     doi: opts == null or opts.doi != "false",
     url: opts == null or opts.url != "false",
     isbn: opts == null or opts.isbn != "false",
     backend: if (opts != null and opts.backend != null) opts.backend else "biber",
     language: if (language == null) "english" else language}
}

pub fn locale(language) {
    if (language == "german" or language == "ngerman" or language == "de")
        {and: "und", etal: "u. a.", references: "Literatur", in_word: "In:",
         editor: "Hrsg.", accessed: "Zugriff", no_date: "o. J."}
    else if (language == "french" or language == "fr")
        {and: "et", etal: "et al.", references: "Références", in_word: "Dans :",
         editor: "dir.", accessed: "consulté le", no_date: "s. d."}
    else
        {and: "and", etal: "et al.", references: "References", in_word: "In:",
         editor: "ed.", accessed: "accessed", no_date: "n.d."}
}

pub fn date_text(entry, language) {
    let f = entry.fields
    if (f.date != null and f.date != "") f.date
    else if (f.year != null and f.year != "") f.year
    else locale(language).no_date
}

pub fn year_text(entry, language) {
    let date_value = date_text(entry, language)
    if (len(date_value) >= 4 and (int(slice(date_value, 0, 4)) ^ { null }) != null)
        slice(date_value, 0, 4)
    else date_value
}

fn sort_author(entry) {
    let f = entry.fields
    if (f.sortname != null) lower(f.sortname)
    else {
        let parsed = names.primary(entry)
        if (len(parsed) == 0) lower(if (f.sorttitle != null) f.sorttitle else
            if (f.title != null) f.title else entry.key)
        else names.sort_name(parsed)
    }
}

fn sort_title(entry) {
    let f = entry.fields
    lower(if (f.sorttitle != null) f.sorttitle else
        if (f.title != null) f.title else entry.key)
}

fn sort_year(entry) {
    let f = entry.fields
    if (f.sortyear != null) f.sortyear else year_text(entry, "english")
}

fn sort_key(entry, sorting) {
    let f = entry.fields
    let fixed = if (f.sortkey != null) f.sortkey else null
    let name = sort_author(entry)
    let title = sort_title(entry)
    let year = sort_year(entry)
    let main = if (fixed != null) lower(fixed)
        else if (sorting == "nyt") name ++ "|" ++ year ++ "|" ++ title
        else if (sorting == "ynt") year ++ "|" ++ name ++ "|" ++ title
        else name ++ "|" ++ title ++ "|" ++ year
    main ++ "|" ++ entry.key
}

pub fn sort_entries(entries, sorting) {
    if (sorting == "none") entries
    else sort(entries, (entry) => sort_key(entry, sorting))
}

fn alphabetic_base(entry, language) {
    let parsed = names.primary(entry)
    let year = year_text(entry, language)
    let digits = if (len(year) >= 2) slice(year, len(year) - 2, len(year)) else "00"
    let prefix = if (len(parsed) == 0) slice(entry.key, 0, 3)
        else if (len(parsed) == 1) slice(parsed[0].family, 0, 3)
        else if (len(parsed) == 2) slice(parsed[0].family, 0, 1) ++
            slice(parsed[1].family, 0, 1)
        else join([for (i in 0 to 2) slice(parsed[i].family, 0, 1)], "")
    prefix ++ digits
}

fn suffix(index) {
    if (index < 26) chr(97 + index)
    else suffix(int(index / 26) - 1) ++ chr(97 + index % 26)
}

fn same_label(entry, other, style, language) {
    if (style == "alphabetic")
        alphabetic_base(entry, language) == alphabetic_base(other, language)
    else names.family_key(names.primary(entry)) ==
        names.family_key(names.primary(other)) and
        year_text(entry, language) == year_text(other, language)
}

fn unique_name_mode(entries, entry) {
    let parsed = names.primary(entry)
    let family = names.family_key(parsed)
    let full = names.sort_name(parsed)
    let initials = names.initial_key(parsed)
    let collisions = [for (other in entries where
        different_name(other, family, full)) other]
    if (family == "" or len(collisions) == 0) "family"
    else if (any([for (other in collisions)
        names.initial_key(names.primary(other)) == initials])) "full"
    else "init"
}

fn different_name(entry, family, full) {
    let parsed = names.primary(entry)
    names.family_key(parsed) == family and names.sort_name(parsed) != full
}

fn collision_count(entries, entry, style, language) {
    len([for (other in entries where same_label(entry, other, style, language)) other])
}

fn earlier_collisions(entries, index, style, language) {
    if (index == 0) 0
    else len([for (i in 0 to (index - 1)
        where same_label(entries[index], entries[i], style, language)) i])
}

fn label_one(entries, index, style, language) {
    let entry = entries[index]
    let collisions = collision_count(entries, entry, style, language)
    let disambiguator = if (collisions > 1)
        suffix(earlier_collisions(entries, index, style, language)) else ""
    let year = year_text(entry, language)
    let alpha = alphabetic_base(entry, language)
    let label = if (style == "alphabetic") alpha ++ disambiguator
        else if (style == "authoryear" or style == "authoryear-comp")
            year ++ disambiguator
        else string(index + 1)
    {*:entry, number: index + 1, label: label,
        year_label: year ++ disambiguator,
        unique_name: unique_name_mode(entries, entry)}
}

pub fn assign_labels(entries, style, language) {
    [for (i, entry in entries) label_one(entries, i, style, language)]
}

fn entry_language(entry, settings) {
    if (entry.fields.langid != null) entry.fields.langid else settings.language
}

pub fn author_text(entry, settings, citation) {
    let language = entry_language(entry, settings)
    let words = locale(language)
    let parsed = names.primary(entry)
    if (len(parsed) == 0) if (entry.fields.title != null) entry.fields.title else entry.key
    else if (citation) {
        let has_year_suffix = entry.year_label != null and
            entry.year_label != year_text(entry, language)
        let mode = if (settings.uniquename == "false" or
            (settings.uniquename == "init" and has_year_suffix)) "family"
            else if (settings.uniquename == "full" and entry.unique_name != "family") "full"
            else entry.unique_name
        names.citation(parsed, settings.maxcitenames, settings.mincitenames,
            words.and, words.etal, mode)
    }
    else names.display(parsed, settings.maxbibnames, settings.minbibnames,
        settings.bibstyle != "numeric" and settings.bibstyle != "numeric-comp" and
            settings.bibstyle != "alphabetic",
        settings.giveninits, words.and, words.etal)
}

pub fn short_author(entry, settings) {
    let parsed = names.primary(entry)
    if (len(parsed) == 0)
        if (entry.fields.title != null) entry.fields.title else entry.key
    else {
        let words = locale(entry_language(entry, settings))
        names.short(parsed, words.and, words.etal)
    }
}

pub fn same_author(entry, other) =>
    len(names.primary(entry)) > 0 and len(names.primary(other)) > 0 and
    names.sort_name(names.primary(entry)) == names.sort_name(names.primary(other))

fn nonempty(parts) => [for (part in parts where part != null and part != "") part]

fn quoted_title(entry) => entry.kind == "article" or entry.kind == "inbook" or
    entry.kind == "incollection" or entry.kind == "inproceedings" or
    entry.kind == "unpublished"

pub fn citation_title(entry) {
    let title = if (entry.fields.title != null) entry.fields.title
        else if (entry.fields.booktitle != null) entry.fields.booktitle else entry.key
    if (quoted_title(entry)) "“" ++ title ++ "”" else title
}

fn publication(entry, settings, include_date) {
    let f = entry.fields
    let lang = entry_language(entry, settings)
    let date_value = if (include_date) date_text(entry, lang) else null
    let place = if (f.location != null and f.publisher != null)
        f.location ++ ": " ++ f.publisher
        else if (f.publisher != null) f.publisher else f.location
    if (entry.kind == "article") {
        let journal = if (f.journaltitle != null) f.journaltitle else f.journal
        let volume = if (f.volume != null) f.volume ++
            (if (f.number != null) "(" ++ f.number ++ ")" else "") else f.number
        // the journal, volume and issue form one unit before the date.
        let periodical = if (journal == null) volume
            else locale(lang).in_word ++ " " ++ journal ++
                (if (volume != null) " " ++ volume else "")
        let dated = if (date_value == null) periodical
            else if (periodical == null) "(" ++ date_value ++ ")"
            else periodical ++ " (" ++ date_value ++ ")"
        join(nonempty([dated, f.pages]), ", ")
    }
    else if (entry.kind == "inbook" or entry.kind == "incollection" or
             entry.kind == "inproceedings")
        join(nonempty([if (f.booktitle != null)
            locale(lang).in_word ++ " " ++ f.booktitle else null,
            if (f.editor != null) f.editor ++ " (" ++ locale(lang).editor ++ ")" else null,
            f.pages, place, date_value]), ", ")
    else if (entry.kind == "thesis" or entry.kind == "report")
        join(nonempty([f.type, f.institution, date_value]), ", ")
    else if (entry.kind == "online")
        join(nonempty([date_value, if (f.urldate != null)
            locale(lang).accessed ++ " " ++ f.urldate else null]), ", ")
    else join(nonempty([f.edition, f.volume, place, date_value]), ", ")
}

pub fn bibliography_text(entry, settings) {
    let f = entry.fields
    let author = if (len(names.primary(entry)) == 0) ""
        else author_text(entry, settings, false)
    let title = if (f.title != null or f.booktitle != null)
        citation_title(entry) else ""
    let author_year = settings.bibstyle == "authoryear" or
        settings.bibstyle == "authoryear-comp"
    let label_year = if (entry.year_label != null) entry.year_label
        else year_text(entry, entry_language(entry, settings))
    let lead = if (author_year and author != "") author ++ " (" ++
        label_year ++ ")" else author
    let body = publication(entry, settings, not author_year)
    let main = join(nonempty([lead, title, body]), ". ")
    main ++ (if (main == "" or ends_with(main, ".")) "" else ".")
}
