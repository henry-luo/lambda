// S16.10.1v2: an import alias is a binding, so a keyword alias is rejected at
// the import line. grammar.js still accepts this known corner case; the C
// parser correctly rejects it and the discrepancy is reviewed under D8.1.2v3.
import edit: .keyword_shadow_module
1
