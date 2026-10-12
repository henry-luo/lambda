// Canonical references reuse the package's BibTeX parser, name parser and inheritance.
import c: .common
import bib: ~~.packages.bib_data
import people: ~~.packages.bib_names
import markup: .markup

pub let TYPES = {article: "article-journal", book: "book", booklet: "pamphlet",
    inbook: "chapter", incollection: "chapter", inproceedings: "paper-conference",
    conference: "paper-conference", proceedings: "book", collection: "book",
    mvbook: "book", mvcollection: "book", reference: "book", inreference: "entry-encyclopedia",
    thesis: "thesis", phdthesis: "thesis", mastersthesis: "thesis", report: "report",
    techreport: "report", online: "webpage", electronic: "webpage", www: "webpage",
    misc: "document", unpublished: "manuscript", patent: "patent", dataset: "dataset",
    software: "software", manual: "book", periodical: "periodical"}
let TEXT_FIELDS = {title: "title", shorttitle: "title-short", subtitle: "subtitle",
    booktitle: "container-title", journaltitle: "container-title", journal: "container-title",
    shortjournal: "container-title-short", series: "collection-title", publisher: "publisher",
    institution: "publisher", school: "publisher", location: "publisher-place",
    address: "publisher-place", edition: "edition", volume: "volume", number: "issue",
    issue: "issue", pages: "page", pagetotal: "number-of-pages", volumes: "number-of-volumes",
    doi: "DOI", url: "URL", isbn: "ISBN", issn: "ISSN", note: "note", abstract: "abstract",
    language: "language", langid: "language", eventtitle: "event-title", venue: "event-place",
    version: "version", eprint: "archive_location", eprinttype: "archive", type: "genre"}
let NAME_FIELDS = ["author", "editor", "translator", "director", "composer", "recipient",
    "interviewer", "illustrator", "original-author", "container-author", "collection-editor"]
let DATE_FIELDS = {date: "issued", origdate: "original-date", urldate: "accessed", eventdate: "event-date"}
let MONTHS = ["january", "february", "march", "april", "may", "june", "july", "august",
    "september", "october", "november", "december"]

fn month(value) {
    let numeric = int(value) ^ { null }
    let found = [for (i, item in MONTHS where starts_with(item, lower(c.text(value)))) i + 1]
    if (numeric != null) numeric else if (len(found) > 0) found[0] else null
}

fn date_value(value) {
    let ends = split(value, "/")
    let parts = [for (part in ends) [for (item in split(part, "-")) int(item) ^ { null }]]
    if (all([for (part in parts) len(part) > 0 and all([for (item in part) item != null])]))
        {'date-parts': parts}
    else {literal: value}
}

fn person(value, file) {
    if (value.raw == "others") {literal: "others"}
    else if (value.literal != null)
        {literal: markup.bibtex(value.literal, file).text}
    else {family: markup.bibtex(value.family, file).text,
        given: markup.bibtex(value.given, file).text,
        'non-dropping-particle': markup.bibtex(value.prefix, file).text,
        suffix: markup.bibtex(value.suffix, file).text}
}

pub fn from_entry(entry, entries = []) {
    let fields = c.get(entry, "raw_fields", entry.fields)
    let decoded = [for (key, target in TEXT_FIELDS where fields[key] != null)
        {key: target, decoded: markup.bibtex(fields[key], entry.resource)}]
    let dates = c.dictionary([for (key, target in DATE_FIELDS where fields[key] != null)
        {key: target, value: date_value(fields[key])}])
    let year = int(fields.year) ^ { null }
    let issued = if (dates.issued != null) dates.issued else if (year != null)
        {'date-parts': [[year] ++ (if (fields.month == null) [] else [month(fields.month)])]}
        else null
    let names = c.dictionary([for (key in NAME_FIELDS where fields[key] != null)
        {key: key, value: [for (item in people.parse(fields[key])) person(item, entry.resource)]}])
    let values = c.dictionary([for (part in decoded) {key: part.key, value: part.decoded.text}])
    let rich = c.dictionary([for (part in decoded) {key: part.key, value: part.decoded.content}])
    let combined_title = if (values.subtitle == null) values.title else
        c.text(values.title) ++ ": " ++ values.subtitle
    let genre = if (entry.kind == "phdthesis") "PhD thesis"
        else if (entry.kind == "mastersthesis") "Master's thesis" else values.genre
    let parents = [for (candidate in entries where candidate.key == entry.fields.crossref) candidate]
    let inherited_container = if (c.has(["chapter", "paper-conference"], TYPES[entry.kind]))
        markup.bibtex(parents[0].raw_fields.title, entry.resource) else null
    let reference = {*:values, *:names, *:dates, id: entry.key, key: entry.key,
        type: TYPES[entry.kind], title: combined_title, issued: issued, genre: genre,
        'container-title': c.get(values, "container-title", inherited_container.text),
        rich: {*:rich, 'container-title': c.get(rich, "container-title", inherited_container.content),
            title: if (values.subtitle == null) rich.title
            else [rich.title, ": ", rich.subtitle]}, provenance: entry,
        'page-first': if (values.page == null) null else split(values.page, "–")[0]}
    {reference: reference, diagnostics: [for (part in decoded, issue in part.decoded.diagnostics) issue]}
}

pub fn bibtex(source, file = "<bibliography>") {
    let parsed = bib.parse_source(source, file, [], [for (key, value in TYPES) string(key)])
    normalize_entries(bib.finalize(parsed.entries, parsed.diagnostics))
}

pub fn normalize_entries(loaded) {
    let decoded = [for (entry in loaded.entries where entry.kind != "xdata") from_entry(entry, loaded.entries)]
    {references: [for (item in decoded) item.reference], entries: loaded.entries,
        diagnostics: loaded.diagnostics ++ [for (item in decoded, issue in item.diagnostics) issue]}
}

pub fn json(value, file = "<references>") map^ {
    let items = if (value is string) parse(value, "json")^ else value
    if (not (items is array or items is list))
        raise c.failure("invalid-csl-json", "CSL JSON must be an array", file)
    else {
        let decoded = [for (item in items) json_item(item, file)^]
        let ids = [for (item in decoded) item.reference.id]
        if (len(unique(ids)) != len(ids))
            raise c.failure("duplicate-csl-id", "Duplicate CSL JSON reference id", file)
        else {references: [for (item in decoded) item.reference],
            diagnostics: [for (item in decoded, issue in item.diagnostics) issue]}
    }
}

fn json_item(item, file) map^ {
    if (not (item is map) or not (item.id is string or item.id is int) or not (item.type is string))
        raise c.failure("invalid-csl-item", "CSL reference requires id and type", file)
    else if (not valid_names(item) or not valid_dates(item))
        raise c.failure("invalid-csl-item", "CSL names and dates must use structured JSON values", file)
    else {
        let strings = [for (key, value in item where value is string)
            {key: string(key), decoded: if (c.has(["id", "type", "DOI", "URL"], string(key)))
                {text: value, content: [value], diagnostics: []} else markup.csl(value, file)}]
        let values = c.dictionary([for (part in strings) {key: part.key, value: part.decoded.text}])
        let rich = c.dictionary([for (part in strings) {key: part.key, value: part.decoded.content}])
        {reference: {*:item, *:values, id: string(item.id), key: string(item.id), rich: rich,
            'page-first': c.get(item, "page-first", if (item.page == null) null else
                split(replace(item.page, "-", "–"), "–")[0]),
            provenance: {resource: file, resource_offset: 0, resource_line: 1}},
            diagnostics: [for (part in strings, issue in part.decoded.diagnostics) issue]}
    }
}

fn valid_names(item) => all([for (key in NAME_FIELDS where item[key] != null)
    (item[key] is array or item[key] is list) and all([for (person in item[key])
        person is map and (person.literal is string or person.family is string or person.given is string)])])

fn valid_dates(item) => all([for (key in ["issued", "accessed", "original-date", "event-date", "submitted"]
    where item[key] != null) {
    let value = item[key]
    let parts = value["date-parts"]
    value is map and (value.literal is string or value.raw is string or
        ((parts is array or parts is list) and len(parts) <= 2 and all([for (row in parts)
            (row is array or row is list) and len(row) <= 3 and
                all([for (part in row) part == null or (int(part) ^ { null }) != null])])) )
}])
