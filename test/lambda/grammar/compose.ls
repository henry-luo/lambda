// §4.5 composition: `import g` brings g's rules in; a local rule of the same
// name overrides it, and the imported rules then call the override. An import
// written before every local rule supplies the entry (GM5.1).
grammar base {
    list   = item, (",", item)*
    item   = word | number
    word   := "\a"+
    number := "\d"+
    space  := " "*
}
grammar ext {
    import base
    item   = word | number | quoted
    quoted = "<", word, ">"
}
grammar only {
    import base
}
grammar local_first {
    pair = word, "=", item
    import base
}
parse("a, 1, b", base)^
parse("a, <q>, 2", ext)^
parse("x, 9", only)^
parse("k = 3", local_first)^
"a, <q>" is base
"a, <q>" is ext

// GM11: the third argument may be an options map; spans are byte offsets
parse("a, 1", base, {rule: 'list', spans: true})^
parse("  a ", base, {spans: true})^
parse("7", base, {rule: 'number'})^
parse("a", base, {spans: 1}) ^ { ^.message }
