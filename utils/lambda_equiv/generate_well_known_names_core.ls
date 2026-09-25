// Native Lambda port of the catalog generation in utils/generate_well_known_names.py.
// The Python catalog file remains the spelling authority; this parses its data declarations.
import .text_encoding
pn between(source: string, first: string, end_marker: string) {
    let opening = index_of(source, first)
    if (opening == null) { raise error("missing catalog declaration: " ++ first) }
    let start = opening + len(first)
    let closing = index_of(slice(source, start), end_marker)
    if (closing == null) { raise error("unterminated catalog declaration: " ++ first) }
    return slice(source, start, start + closing)
}

pn read_quoted(source: string, start: int) {
    let opening = index_of(slice(source, start), "\"")
    if (opening == null) { return null }
    let begin = start + opening + 1
    let closing = index_of(slice(source, begin), "\"")
    if (closing == null) { return null }
    let end = begin + closing
    return {value: slice(source, begin, end), next: end + 1}
}

pn tuple_entries(source: string) {
    var entries = []
    var pos = 0
    while (pos < len(source)) {
        let opening = index_of(slice(source, pos), "(")
        if (opening == null) { break }
        let start = pos + opening + 1
        let closing = index_of(slice(source, start), ")")
        if (closing == null) { raise error("unterminated catalog tuple") }
        let end = start + closing
        let body = slice(source, start, end)
        let symbol_part = read_quoted(body, 0)
        let spelling = if (symbol_part != null) read_quoted(body, symbol_part.next) else null
        let kind = if (spelling != null) read_quoted(body, spelling.next) else null
        if (symbol_part != null and spelling != null) {
            entries = entries ++ [{symbol: symbol_part.value, spelling: spelling.value,
                                   kind: if (kind != null) kind.value else "STRING"}]
        }
        pos = end + 1
    }
    return entries
}

fn css_symbol(spelling: string) =>
    replace(replace(upper(if (starts_with(spelling, "-")) slice(spelling, 1) else spelling),
                    "-", "_"), "*", "STAR")

pn catalogs() {
    let source = input("utils/well_known_names_data.py", "text")^
    let tags = split(between(source, "MARKUP_TAGS = \"\"\"", "\"\"\""), null)
    let css = split(between(source, "CSS_PROPERTY_SPELLINGS = \"\"\"", "\"\"\""), null)
    let extra = tuple_entries(between(source, "MARKUP_EXTRA_ENTRIES = [", "]"))
    let lambda_entries = tuple_entries(between(source, "1: (\"lambda\", [", "]),"))
    let js_entries = tuple_entries(between(source, "2: (\"js\", [", "]),"))

    var markup = []
    var owners = {}
    for (tag in tags) {
        let spelling = lower(replace(tag, "_", "-"))
        markup = markup ++ [{symbol: tag, spelling: spelling, kind: "STRING"}]
        owners[spelling] = tag
    }
    for (entry in extra) {
        markup = markup ++ [entry]
        owners[entry.spelling] = entry.symbol
    }
    for (spelling in css) {
        if (owners[spelling] == null) {
            markup = markup ++ [{symbol: "CSS_" ++ css_symbol(spelling),
                                 spelling: spelling, kind: "STRING"}]
        }
    }
    return {pools: [
        {pool: 0, label: "markup", entries: markup},
        {pool: 1, label: "lambda", entries: lambda_entries},
        {pool: 2, label: "js", entries: js_entries}
    ], css: css, owners: owners}
}

pn fnv1a(source: string) {
    var value = 2166136261i64
    for (byte in utf8_bytes(source)) {
        value = band(bxor(value, byte + 0i64) * 16777619i64, 4294967295i64)
    }
    return if (value == 0) 1i64 else value
}

pn validate_catalogs(catalog) {
    var seen = {}
    for (pool in catalog.pools) {
        if (pool.pool < 0 or pool.pool > 2 or len(pool.entries) > 65535) {
            raise error("invalid global name pool")
        }
        for (entry in pool.entries) {
            if (entry.kind != "STRING" and entry.kind != "SYMBOL") {
                raise error("invalid key kind for " ++ entry.symbol)
            }
            if (entry.symbol == "" or entry.spelling == "") {
                raise error("empty catalog symbol or spelling")
            }
            if (seen[entry.spelling] != null) {
                raise error("duplicate global spelling without an alias: " ++ entry.spelling)
            }
            seen[entry.spelling] = entry.symbol
        }
    }
    var css_seen = {}
    for (spelling in catalog.css) {
        if (css_seen[spelling] != null) { raise error("duplicate CSS property spelling") }
        if (seen[spelling] == null) {
            raise error("CSS property missing generated NameId: " ++ spelling)
        }
        css_seen[spelling] = true
    }
    return
}

pn render_header(pool) {
    let label = pool.label
    let prefix = upper(label)
    let type_name = if (label == "js") "JsNameId" else if (label == "lambda") "LambdaNameId" else "MarkupNameId"
    let identity = if (label == "js") "../core/name_identity.h" else "name_identity.h"
    var out = "// Generated by utils/generate_well_known_names.py; do not edit.\n" ++
              "#pragma once\n#include \"" ++ identity ++ "\"\n\n" ++
              "enum " ++ type_name ++ " {\n"
    var ordinal = 1
    for (entry in pool.entries) {
        let enum_prefix = prefix ++ (if (entry.kind == "SYMBOL") "_SYMBOL" else "_NAME")
        let name_id = shl(pool.pool, 16) + ordinal
        out = out ++ "    " ++ enum_prefix ++ "_" ++ entry.symbol ++ " = 0x" ++
              hex8(name_id) ++ "u,\n"
        ordinal = ordinal + 1
    }
    out = out ++ "};\n\nextern const WellKnownNameRecord g_well_known_" ++ label ++
          "_names[];\nextern const size_t g_well_known_" ++ label ++ "_name_count;\n"
    return out
}

pn render_source(pool) {
    let label = pool.label
    let header = if (label == "js") "js_well_known_names.h" else "well_known_" ++ label ++ "_names.h"
    var out = "// Generated by utils/generate_well_known_names.py; do not edit.\n" ++
              "#include \"" ++ header ++ "\"\n\n" ++
              "const WellKnownNameRecord g_well_known_" ++ label ++ "_names[] = {\n"
    var ordinal = 1
    for (entry in pool.entries) {
        let bytes = utf8_bytes(entry.spelling)
        if (len(bytes) >= 127) { raise error("spelling too long: " ++ entry.spelling) }
        let key_kind = if (entry.kind == "SYMBOL") "NAME_KEY_SYMBOL" else "NAME_KEY_STRING"
        let name_id = shl(pool.pool, 16) + ordinal
        let spelling = replace(replace(entry.spelling, "\\", "\\\\"), "\"", "\\\"")
        out = out ++ "    { { 0x" ++ hex8(fnv1a(entry.spelling)) ++
              "u, UINT32_MAX, 0, " ++ key_kind ++ ", 0, 0x" ++ hex8(name_id) ++
              "u }, " ++ string(len(bytes)) ++ "u, 0x05u, \"" ++ spelling ++ "\" },\n"
        ordinal = ordinal + 1
    }
    out = out ++ "};\nconst size_t g_well_known_" ++ label ++ "_name_count = " ++
          "sizeof(g_well_known_" ++ label ++ "_names) / " ++
          "sizeof(g_well_known_" ++ label ++ "_names[0]);\n"
    return out
}

pn render_css_mapping(catalog) {
    let slash = chr(92)
    var out = "// Generated by utils/generate_well_known_names.py; do not edit.\n" ++
              "#pragma once\n#include \"../../core/well_known_markup_names.h\"\n\n" ++
              "// X(dense CssPropertyCode, generated NameId)\n" ++
              "#define WELL_KNOWN_CSS_PROPERTY_NAME_IDS(X) " ++ slash ++ "\n"
    var i = 0
    for (spelling in catalog.css) {
        let owner = catalog.owners[spelling]
        let name_id = if (owner != null) "MARKUP_NAME_" ++ owner
                      else "MARKUP_NAME_CSS_" ++ css_symbol(spelling)
        let suffix = if (i + 1 < len(catalog.css)) " " ++ slash else ""
        out = out ++ "    X(CSS_PROPERTY_" ++ css_symbol(spelling) ++ ", " ++ name_id ++
              ")" ++ suffix ++ "\n"
        i = i + 1
    }
    return out
}

pn render_lookup(catalog) {
    var count = 0
    for (pool in catalog.pools) { count = count + len(pool.entries) }
    var capacity = 1
    while (count * 20 > capacity * 7) { capacity = capacity * 2 }
    var slots = fill(capacity, 0)
    var max_probe = 0
    for (pool in catalog.pools) {
        var ordinal = 1
        for (entry in pool.entries) {
            let name_id = shl(pool.pool, 16) + ordinal
            var slot = band(fnv1a(entry.spelling), capacity - 1)
            var probe = 0
            while (slots[slot] != 0) {
                slot = band(slot + 1, capacity - 1)
                probe = probe + 1
                if (probe >= capacity) { raise error("unable to place generated name lookup entry") }
            }
            slots[slot] = name_id
            max_probe = max(max_probe, probe)
            ordinal = ordinal + 1
        }
    }
    var out = "// Generated by utils/generate_well_known_names.py; do not edit.\n" ++
              "#pragma once\n#include \"name_identity.h\"\n\n" ++
              "// Combined immutable lookup table: " ++ string(count) ++ " records, " ++
              string(capacity) ++ " slots, max insertion probe " ++ string(max_probe) ++ ".\n" ++
              "static const NameId g_well_known_name_lookup[" ++ string(capacity) ++ "] = {\n"
    var offset = 0
    while (offset < capacity) {
        var row = []
        var i = offset
        while (i < min(offset + 8, capacity)) {
            row = row ++ ["0x" ++ hex8(slots[i]) ++ "u"]
            i = i + 1
        }
        out = out ++ "    " ++ join(row, ", ") ++ ",\n"
        offset = offset + 8
    }
    out = out ++ "};\nstatic const size_t g_well_known_name_lookup_capacity = " ++
          string(capacity) ++ ";\n" ++
          "static const size_t g_well_known_name_lookup_max_probe = " ++
          string(max_probe) ++ ";\n"
    return out
}

pub pn generated_outputs() {
    let catalog = catalogs()
    validate_catalogs(catalog)
    var outputs = []
    for (pool in catalog.pools) {
        let label = pool.label
        let base = if (label == "js") "lambda/js/" else "lambda/core/"
        let stem = if (label == "js") "js_well_known_names" else "well_known_" ++ label ++ "_names"
        outputs = outputs ++ [
            {path: base ++ stem ++ ".h", content: render_header(pool)},
            {path: base ++ stem ++ ".c", content: render_source(pool)}
        ]
    }
    outputs = outputs ++ [
        {path: "lambda/input/css/well_known_css_property_mapping.h", content: render_css_mapping(catalog)},
        {path: "lambda/core/well_known_name_lookup.h", content: render_lookup(catalog)}
    ]
    return outputs
}
