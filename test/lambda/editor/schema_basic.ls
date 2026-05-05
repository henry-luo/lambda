// Schema lookup and child-role classification (Phase R1)
import lambda.package.editor.mod_md_schema
import lambda.package.editor.mod_edit_schema

// 1. Schema entry lookup by tag symbol
let sch = md_schema
"paragraph role:"; sch.paragraph.role
"strong role:"; sch.strong.role
"hr role:"; sch.hr.role
"image role:"; sch.image.role
"missing tag is null:"; sch.no_such_tag == null

// 2. child_role — strings, declared elements, undeclared elements
"text role:"; child_role(sch, "hello")
"paragraph role:"; child_role(sch, <paragraph; "x">)
"strong role:"; child_role(sch, <strong; "x">)
"unknown role:"; child_role(sch, <madeup; "x">)
"int is unknown:"; child_role(sch, 42)

// 3. Default block for splitting
"default block:"; md_default_block

// 4. satisfies() — basic role-based and tag-based terms
"text sat inline*:"; satisfies(sch, "abc", {role: 'inline', qty: 'star'})
"strong sat inline*:"; satisfies(sch, <strong; "x">, {role: 'inline', qty: 'star'})
"paragraph sat block+:"; satisfies(sch, <paragraph; "x">, {role: 'block', qty: 'plus'})
"text NOT sat block:"; satisfies(sch, "abc", {role: 'block', qty: 'star'})
"tag-keyed match:"; satisfies(sch, <paragraph; "x">, {tag: 'paragraph', qty: 'one'})
"tag-keyed mismatch:"; satisfies(sch, <heading; "x">, {tag: 'paragraph', qty: 'one'})

// 5. match_content — sequence matching
"empty matches empty:"; match_content(sch, [], [])
"inline* matches strings:"; match_content(sch, ["a", "b"], [{role: 'inline', qty: 'star'}])
"block+ needs at least one:"; match_content(sch, [], [{role: 'block', qty: 'plus'}])
"block+ ok with one block:"; match_content(sch, [<paragraph; "x">], [{role: 'block', qty: 'plus'}])
"sequence p then block*:"; match_content(sch,
  [<paragraph; "x">, <paragraph; "y">],
  [{tag: 'paragraph', qty: 'one'}, {role: 'block', qty: 'star'}])
"sequence rejects wrong head:"; match_content(sch,
  [<heading; "x">],
  [{tag: 'paragraph', qty: 'one'}])
