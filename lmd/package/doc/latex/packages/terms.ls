// latex/packages/terms.ls — one term registry for glossaries, acronym, nomencl and
// the index (makeidx, imakeidx) (vibe/impl/Lambda_Impl_Latex_Packages.md §11).
// Analysis collects every definition and every use in source order, so a use knows
// whether it is the first (S12.1.1v2: no mutation); render prints the uses and
// the lists. Field values are TeX source, rendered through util.parse_fragment.

import util: ~~.util

// the registry analysis starts from: definitions (gls_entries, gls_titles, ac_entries,
// nomencl, index_entries), each use's first-use flag by offset (gls_uses, ac_uses),
// the labels whose first use is over (gls_firsts, ac_firsts), the listed glossary
// labels and the printed glossary types
pub let EMPTY = {gls_entries: [], gls_titles: [], gls_firsts: [], gls_listed: [], gls_uses: [],
    gls_printed: [], ac_entries: [], ac_firsts: [], ac_uses: [], nomencl: [], index_entries: []}

// ---- glossaries ---------------------------------------------------------------------

// how each \gls-like command prints an entry: the field ("auto" chooses the first or
// later form), plural, capitalisation ("first" or "all"), whether it ends the first
// use, and whether it lists the entry
let G = (field, plural, cap, ends) => {field: field, plural: plural, cap: cap, ends_first: ends, lists: true}
let GLS_FORMS = {
    gls: G("auto", false, null, true), Gls: G("auto", false, "first", true), GLS: G("auto", false, "all", true),
    glspl: G("auto", true, null, true), Glspl: G("auto", true, "first", true), GLSpl: G("auto", true, "all", true),
    glstext: G("text", false, null, false), Glstext: G("text", false, "first", false),
    glsfirst: G("first", false, null, false), Glsfirst: G("first", false, "first", false),
    glsplural: G("text", true, null, false), Glsplural: G("text", true, "first", false),
    glsfirstplural: G("first", true, null, false),
    glsname: G("name", false, null, false), Glsname: G("name", false, "first", false),
    glsdesc: G("description", false, null, false), Glsdesc: G("description", false, "first", false),
    glssymbol: G("symbol", false, null, false), Glssymbol: G("symbol", false, "first", false),
    acrshort: G("short", false, null, false), Acrshort: G("short", false, "first", false),
    ACRshort: G("short", false, "all", false), acrshortpl: G("short", true, null, false),
    Acrshortpl: G("short", true, "first", false),
    acrlong: G("long", false, null, false), Acrlong: G("long", false, "first", false),
    ACRlong: G("long", false, "all", false), acrlongpl: G("long", true, null, false),
    Acrlongpl: G("long", true, "first", false),
    acrfull: G("full", false, null, false), Acrfull: G("full", false, "first", false),
    ACRfull: G("full", false, "all", false), acrfullpl: G("full", true, null, false),
    Acrfullpl: G("full", true, "first", false),
    glsdisp: G("given", false, null, true), glslink: G("given", false, null, false)}
// \glsentry… print a field without linking or listing
let ENTRY_FIELDS = {glsentryname: "name", glsentrytext: "text", glsentrydesc: "description",
    glsentrysymbol: "symbol", glsentryfirst: "first", glsentryplural: "plural",
    Glsentryname: "name", Glsentrytext: "text", Glsentrydesc: "description"}

pub fn gls_form(command) {
    if (GLS_FORMS[command] != null) GLS_FORMS[command]
    else if (ENTRY_FIELDS[command] != null)
        {field: ENTRY_FIELDS[command], plural: false, cap: if (starts_with(command, "G")) "first" else null,
         ends_first: false, lists: false}
    else null
}

pub let GLS_USE_COMMANDS = [for (k, v at GLS_FORMS) string(k)] ++ [for (k, v at ENTRY_FIELDS) string(k)]
pub let GLOSSARIES_COMMANDS = GLS_USE_COMMANDS ++ ["newglossaryentry", "longnewglossaryentry", "newacronym",
    "newglossary", "makeglossaries", "printglossary", "printglossaries", "printacronyms", "glsadd",
    "glsaddall", "glsreset", "glsunset", "glsresetall", "glsunsetall", "setacronymstyle",
    "setglossarystyle", "glossarystyle", "loadglsentries"]
pub let GLOSSARIES_PREAMBLE = ["newglossaryentry", "longnewglossaryentry", "newacronym", "newglossary",
    "makeglossaries", "setacronymstyle", "setglossarystyle", "glossarystyle", "loadglsentries"]
// the list styles are drawn as a description list; the others report
// glossary-style-approximation
pub let LIST_STYLES = ["list", "listgroup", "listhypergroup", "altlist", "altlistgroup", "altlisthypergroup"]
pub fn style_issue(style) => if (style == null or (contains(LIST_STYLES, trim(string(style))) or false)) null
    else trim(string(style))
pub let GLOSSARIES_OPTIONS = ["acronym", "acronyms", "toc", "nonumberlist", "nopostdot", "postdot",
    "nomain", "automake", "xindy", "makeindex", "noredefwarn", "nogroupskip", "savewrites",
    "translate", "notranslate", "numberedsection", "section", "style", "sort", "order", "nolong",
    "nosuper", "notree", "nolist", "nostyles", "indexonlyfirst", "hyperfirst", "nohypertypes"]

// \newglossaryentry{label}{keys}: the entry with its fields defaulted as glossaries
// does (text and first from the name, the plural from the text)
pub fn glossary_entry(key, raw_keys) {
    let keys = util.parse_kv_options(raw_keys)
    let entry_name = field(keys.name)
    let text = field(keys.text) or entry_name
    let plural = field(keys.plural) or (text ++ "s")
    {label: key, kind: "entry", type: field(keys.type) or "main", name: entry_name, text: text,
     first: field(keys.first) or text, plural: plural,
     firstplural: if (field(keys.firstplural) != null) field(keys.firstplural)
        else if (field(keys.first) != null) field(keys.first) ++ "s" else plural,
     description: field(keys.description) or "", symbol: field(keys.symbol),
     sort: field(keys.sort) or entry_name, see: field(keys.see)}
}

fn field(value) => if (value == null) null else unbrace(trim(string(value)))

fn unbrace(text) =>
    if (starts_with(text, "{") and ends_with(text, "}") and len(text) >= 2) slice(text, 1, len(text) - 1) else text

// \newacronym[keys]{label}{short}{long}: in the acronym glossary under the acronym
// option, else in main; its name is the short form and its description the long
pub fn acronym_entry(key, short, long, raw_keys, separate) {
    let keys = util.parse_kv_options(raw_keys)
    let shortplural = field(keys.shortplural) or (short ++ "s")
    let longplural = field(keys.longplural) or (long ++ "s");
    {label: key, kind: "acronym", type: if (separate) "acronym" else "main", name: short, text: short,
     short: short, long: long, shortplural: shortplural, longplural: longplural,
     first: long ++ " (" ++ short ++ ")", plural: shortplural,
     firstplural: longplural ++ " (" ++ shortplural ++ ")",
     description: field(keys.description) or long, symbol: null, sort: field(keys.sort) or short, see: null}
}

// the source an entry's use prints
pub fn use_source(entry, form, first) {
    let chosen = if (form.field == "auto") (if (first) (if (form.plural) entry.firstplural else entry.first)
            else (if (form.plural) entry.plural else entry.text))
        else if (form.field == "text") (if (form.plural) entry.plural else entry.text)
        else if (form.field == "first") (if (form.plural) entry.firstplural else entry.first)
        else if (form.field == "short") (if (form.plural) entry.shortplural else entry.short)
        else if (form.field == "long") (if (form.plural) entry.longplural else entry.long)
        else if (form.field == "full") (if (form.plural) entry.firstplural else entry.first)
        else entry[form.field]
    let source = chosen or entry.name or ""
    if (form.cap == "first") capitalised(source) else source
}

// the first letter up, when the source starts with one (a command or math is left)
pub fn capitalised(source) {
    let head = slice(source, 0, 1)
    if (head >= "a" and head <= "z") upper(head) ++ slice(source, 1, len(source)) else source
}

// a glossary's title: one \printglossary gave, \newglossary's, or the default
pub fn type_title(gtype, titles, given = null) {
    let custom = util.lookup(titles, gtype)
    if (given != null) given else if (custom != null) custom else if (gtype == "acronym") "Acronyms" else "Glossary"
}

// the macro a document renames a default title with
pub fn name_macro(gtype, given) =>
    if (given != null) null else if (gtype == "acronym") "acronymname" else if (gtype == "main") "glossaryname" else null

// the glossaries a print command prints, as {type, title}: \printglossary[type=…,
// title=…] one, \printacronyms the acronyms, \printglossaries every type in order
// (main unless nomain, acronym under the acronym option, then \newglossary's)
pub fn printed_types(command, raw_options, options, titles) {
    let given = util.parse_kv_options(raw_options)
    if (command == "printacronyms") [{type: "acronym", title: field(given.title)}]
    else if (command == "printglossary") [{type: field(given.type) or "main", title: field(given.title)}]
    else [*(if (util.option_on(options, "nomain")) [] else [{type: "main", title: null}]),
          *(if (util.option_on(options, "acronym") or util.option_on(options, "acronyms")) [{type: "acronym", title: null}] else []),
          *[for (entry in titles) {type: entry.key, title: null}]]
}

// ---- acronym (the package) ---------------------------------------------------------------

// \ac and its family: the form, plural, capitalisation, and whether it ends the first use
let A = (form, plural, cap, ends) => {form: form, plural: plural, cap: cap, ends_first: ends}
let AC_FORMS = {
    ac: A("auto", false, null, true), Ac: A("auto", false, "first", true),
    acp: A("auto", true, null, true), Acp: A("auto", true, "first", true),
    acf: A("full", false, null, true), Acf: A("full", false, "first", true),
    acfp: A("full", true, null, true), Acfp: A("full", true, "first", true),
    acs: A("short", false, null, false), acsp: A("short", true, null, false), acsu: A("short", false, null, true),
    acl: A("long", false, null, false), Acl: A("long", false, "first", false),
    aclp: A("long", true, null, false), Aclp: A("long", true, "first", false), aclu: A("long", false, null, true)}

pub fn ac_form(command) => AC_FORMS[command]

pub let AC_USE_COMMANDS = [for (k, v at AC_FORMS) string(k)]
pub let ACRONYM_COMMANDS = AC_USE_COMMANDS ++ ["acronym", "acro", "acrodef", "acroplural", "acrodefplural",
    "acused", "acresetall"]
// withpage needs pages and footnote moves first uses into notes: both stay out and
// are reported as unsupported options, as is smaller (a font-size change of the short form)
pub let ACRONYM_OPTIONS = ["printonlyused", "nolist", "dua", "nohyperlinks", "single", "macros", "inline"]

// \acro{label}[short]{long} and \acrodef: the short form defaults to the label
pub fn acro_entry(key, short, long) =>
    {label: key, short: short or key, long: long, shortplural: (short or key) ++ "s", longplural: long ++ "s"}

// the parts an \ac-family use prints, each with the font macro that styles it:
// \acffont the long form of a full use, \acfsfont its short form, \acsfont a short
// use. `dua` prints the long form in place of the short one.
pub fn ac_parts(entry, form, first, dua) {
    let short = if (form.plural) entry.shortplural else entry.short
    let long = if (form.plural) entry.longplural else entry.long
    let shape = if (form.form == "full") "full" else if (form.form == "long" or dua) "long"
        else if (form.form == "short") "short" else if (first) "full" else "short"
    let lead = if (form.cap == "first") capitalised(if (shape == "short") short else long) else null
    if (shape == "full") [{source: lead or long, font: "acffont"}, {source: " (", font: null},
        {source: short, font: "acfsfont"}, {source: ")", font: null}]
    else if (shape == "long") [{source: lead or long, font: null}]
    else [{source: lead or short, font: "acsfont"}]
}

// ---- nomencl --------------------------------------------------------------------------

pub let NOMENCL_COMMANDS = ["makenomenclature", "nomenclature", "printnomenclature"]
// refpage and refeq add page and equation references, which are reported
pub let NOMENCL_OPTIONS = ["intoc", "notintoc", "noprefix", "prefix", "english", "norefeq", "norefpage",
    "nocfg", "stdsubgroups", "compatible"]

// nomencl sorts by prefix and then symbol, both as source
pub fn nomencl_key(entry) => lower(entry.prefix ++ entry.symbol)

// ---- the index -------------------------------------------------------------------------

pub let INDEX_COMMANDS = ["makeindex", "printindex", "index", "indexprologue", "indexsetup"]
pub let IMAKEIDX_OPTIONS = ["makeindex", "xindy", "texindy", "truexindy", "noautomatic", "nonewpage",
    "original", "splitindex", "quiet"]

// makeindex's quote character makes the next character literal
fn unquote(text) => replace(replace(replace(replace(text, "\"!", "!"), "\"@", "@"), "\"|", "|"), "\"\"", "\"")

// the positions of `ch` outside braces and not after the quote character
fn cuts(text, ch, i, depth, acc) {
    if (i >= len(text)) acc
    else {
        let c = slice(text, i, i + 1)
        if (c == "\"") cuts(text, ch, i + 2, depth, acc)
        else if (c == "{") cuts(text, ch, i + 1, depth + 1, acc)
        else if (c == "}") cuts(text, ch, i + 1, depth - 1, acc)
        else if (c == ch and depth == 0) cuts(text, ch, i + 1, depth, acc ++ [i])
        else cuts(text, ch, i + 1, depth, acc)
    }
}

fn split_at(text, ch) {
    let at = cuts(text, ch, 0, 0, [])
    let bounds = [-1, *at, len(text)];
    [for (k in 0 to len(bounds) - 2) slice(text, bounds[k] + 1, bounds[k + 1])]
}

// \index{sort@display!sub|encap}: the levels as {sort, display} and the encapsulator:
// `(` and `)` open and close a range, see{…} and seealso{…} refer, any other name
// formats the locator
pub fn index_entry(raw) {
    let bar = cuts(raw, "|", 0, 0, [])
    let entry_text = if (len(bar) == 0) raw else slice(raw, 0, bar[0])
    let encap = if (len(bar) == 0) "" else slice(raw, bar[0] + 1, len(raw))
    let levels = [for (level in split_at(entry_text, "!"), let at = cuts(level, "@", 0, 0, []))
        {sort: unquote(trim(if (len(at) == 0) level else slice(level, 0, at[0]))),
         display: unquote(trim(if (len(at) == 0) level else slice(level, at[0] + 1, len(level))))}]
    let span = if (starts_with(encap, "(")) "open" else if (starts_with(encap, ")")) "close" else null
    let rest = if (span == null) encap else slice(encap, 1, len(encap))
    let refer = if (starts_with(rest, "seealso")) "seealso" else if (starts_with(rest, "see")) "see" else null
    {levels: levels, range: span, refer: refer,
     target: if (refer == null) null else unbrace(trim(slice(rest, len(refer), len(rest)))),
     format: if (refer != null or trim(rest) == "") null else trim(rest)}
}

// the case-folded sort key of an entry path
pub fn index_key(levels) => join([for (level in levels) lower(level.sort)], "\u0001")

// the group an index entry falls in: its sort key's first letter, else symbols
pub fn index_group(levels) {
    let head = lower(slice(levels[0].sort, 0, 1))
    if (head >= "a" and head <= "z") head else "#"
}
