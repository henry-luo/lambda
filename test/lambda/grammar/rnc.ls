// M5: lambda.grammar.rnc parses RELAX NG compact syntax into a Lambda element
// tree. `,` `&` `|` are repetitions that do not mix without parentheses; a
// literal's segments join with `~` and lose their quotes. One line per schema.
import lambda.grammar.rnc

let cases = [
  "element addressBook { element card { element name { text }, element email { text } }* }",
  "default namespace = \"http://example.com/ns\"\nnamespace a = \"urn:a\"\ndatatypes xsd = \"http://www.w3.org/2001/XMLSchema-datatypes\"\nstart = Card\nCard = element card { Name, attribute id { xsd:ID }?, mixed { text } }\nName = element name { xsd:string { maxLength = \"127\" } }",
  "element doc { (a | b), (c & d) }",
  "element * - (foo | ns:*) { attribute lang { \"en\" | \"f\" ~ 'r' ~ \"\" }, list { xsd:integer+ } }",
  "## Documentation line\n## second\n[ a:defaultValue = \"x\" ] start = element root { empty }",
  "\\element = element element { text }",
  "grammar { start = element x { notAllowed } include \"a.rnc\" inherit = foo { start |= y } div { y = parent z } }",
  "element element-name { xsd:token - \"none\" } # comment\n",
  "start = element a { text >> a:note [ \"hi\" ] }",
  "start = element a { string \"\"\"tri\"ple\"\"\" | token '''x''' }",
  "external \"other.rnc\" inherit = a",
  "a &= element b { empty }+ a:x [ y = \"1\" ]"
]
for (c in cases) c ++ "  =>  " ++ format(parse(c, rnc) ^ { "ERROR " ++ ^.message }, "mark") ++ "\n"

// the operators do not mix without parentheses; `element` alone is not an
// identifier; a definition needs a pattern
let errors = ["element doc { a, b | c }", "element doc { a & b, c }", "element = text", "start ="]
for (e in errors) e ++ "  =>  " ++ (parse(e, rnc) ^ { ^.message }) ++ "\n"
