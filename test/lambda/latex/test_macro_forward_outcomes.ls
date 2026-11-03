// S1.6/D8.3.3: a forward call keeps the public Item ABI until native lanes are proven.
import macros: lambda.doc.latex.macros
let valid = macros.get_defs(<document <newcommand "\\greeting" <brack_group "1"> <curly_group "hello">>>)
// Attributes contribute to len; the scanner also reaches absent positional children.
let missing = macros.get_defs(<document <newcommand source_offset: 1, argument_groups: [], "\\dang" <brack_group "1">>>);
[len(valid), valid[0].params, len(missing)]
