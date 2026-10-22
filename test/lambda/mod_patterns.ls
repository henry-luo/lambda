// helper for import_patterns.ls: named string patterns, private ones first so
// a public pattern's index in this module's type list is past the importer's
type digits = \("\d"+)
type spaces = \("\s"+)
pub type word = \(("a" to "z")+)
pub type pair = \(word "=" digits)
pub fn words(s) => [for (m in find(s, word)) m.value]
pub fn gaps(s) => len(find(s, spaces))
