// Schema validation against actual Mark trees (Phase R1)
// Note: Lambda's printer collapses adjacent strings into one line, so this
// suite reports violation messages via boolean comparisons / lengths instead
// of printing the raw strings.
import lambda.package.editor.mod_md_schema
import lambda.package.editor.mod_edit_schema

let sch = md_schema

// 1. A well-formed tiny doc
let doc1 = <doc <paragraph; "Hello, world.">>
"doc1 valid:"; is_valid(sch, doc1)
"doc1 violations:"; len(schema_validate(sch, doc1))

// 2. A document with a strong mark and a heading
let doc2 = <doc
  <heading; "Title">
  <paragraph; "See " <strong; "this">>
>
"doc2 valid:"; is_valid(sch, doc2)
"doc2 violations:"; len(schema_validate(sch, doc2))

// 3. Atomic violation: hr is not allowed to have children
let bad_atomic = <doc <hr; "stuff">>
let v1 = schema_validate(sch, bad_atomic)
"hr-with-kids violations:"; len(v1)
"hr violation msg ok:"; v1[0].message == "atomic node has children"
"hr violation tag:"; v1[0].tag

// 4. Unknown tag — produces both an unknown-tag violation and a content
//    violation on the parent (because 'unknown is not a 'block)
let bad_unknown = <doc <made_up_tag; "x">>
let v2 = schema_validate(sch, bad_unknown)
"unknown violations:"; len(v2)
"unknown msg ok:"; v2[0].message == "content does not match schema"

// 5. doc with no children fails block+
let empty_doc = <doc>
"empty-doc valid:"; is_valid(sch, empty_doc)
let v3 = schema_validate(sch, empty_doc)
"empty-doc msg ok:"; v3[0].message == "content does not match schema"

// 6. list_item missing the required leading paragraph
let bad_li = <doc <list <list_item; <heading; "wrong">>>>
"bad list_item valid:"; is_valid(sch, bad_li)

// 7. list_item with paragraph then nested list (block*) is fine
let nested = <doc <list
  <list_item; <paragraph; "outer"> <list <list_item; <paragraph; "inner">>>>
>>
"nested list valid:"; is_valid(sch, nested)

// 8. Validation on a non-element root
let v4 = schema_validate(sch, "just a string")
"string root violations:"; len(v4)
"string root msg ok:"; v4[0].message == "root is not an element"

// 9. Path tracking — heading inside doc has path [0]
let path_doc = <doc <heading; "T"> <paragraph; "p">>
"path_doc valid:"; is_valid(sch, path_doc)

// inject an unknown nested two-deep to check path tracking
let bad_deep = <doc <paragraph; "p"> <blockquote; <paragraph; <weird; "q">>>>
let vd = schema_validate(sch, bad_deep)
"bad_deep at least one violation:"; (len(vd) >= 1)
"bad_deep first path nonempty:"; (len(vd[0].path) > 0)

// 10. Declared attrs: required and primitive type checks
let img_ok = <doc <paragraph; <image src: "photo.jpg", alt: "A photo">>>
"image attrs valid:"; is_valid(sch, img_ok)
let img_missing = <doc <paragraph; <image alt: "missing src">>>
let vim = schema_validate(sch, img_missing)
"image missing attr:"; vim[0].message == "required attribute missing"
"image missing tag:"; vim[0].tag == 'image'
let img_bad_type = <doc <paragraph; <image src: 42>>>
let vib = schema_validate(sch, img_bad_type)
"image attr type:"; vib[0].message == "attribute type mismatch"
let link_missing = <doc <paragraph; <link; "no href">>>
let vlm = schema_validate(sch, link_missing)
"link missing href:"; vlm[0].tag == 'link' and vlm[0].message == "required attribute missing"

// 11. Attr defaults participate in validation; custom validators reject bad values.
let heading_default = <doc <heading; "Default level">>
"heading default level valid:"; is_valid(sch, heading_default)
let heading_bad = <doc <heading level: 7; "Too deep">>
let vhb = schema_validate(sch, heading_bad)
"heading level validator:"; vhb[0].tag == 'heading' and vhb[0].message == "attribute validation failed"
let heading_bad_type = <doc <heading level: "1"; "Bad type">>
let vhbt = schema_validate(sch, heading_bad_type)
"heading level type first:"; vhbt[0].message == "attribute type mismatch"
let list_default = <doc <list <list_item; <paragraph; "x">>>>
"list ordered default valid:"; is_valid(sch, list_default)
let list_bad_type = <doc <list ordered: "yes"; <list_item; <paragraph; "x">>>>
let vlbt = schema_validate(sch, list_bad_type)
"list ordered type:"; vlbt[0].tag == 'list' and vlbt[0].message == "attribute type mismatch"

// 12. Mark policy: marks can be forbidden by parent entries or excluded by marks.
let strict_para_schema = {
  doc: sch.doc, paragraph: {role: 'block', content: [{role: 'inline', qty: 'star'}], marks: 'none'},
  heading: sch.heading, blockquote: sch.blockquote, list: sch.list, list_item: sch.list_item,
  code_block: sch.code_block, hr: sch.hr, image: sch.image, hard_break: sch.hard_break,
  link: sch.link, strong: sch.strong, em: sch.em, code: sch.code
}
let bad_mark_parent = <doc <paragraph; "x" <strong; "y">>>
let vmp = schema_validate(strict_para_schema, bad_mark_parent)
"mark disallowed by parent:"; vmp[0].tag == 'strong' and vmp[0].message == "mark not allowed"
let bad_nested_mark = <doc <paragraph; <code; <strong; "x">>>>
let vnm = schema_validate(sch, bad_nested_mark)
"mark excluded by code:"; vnm[0].tag == 'strong' and vnm[0].message == "mark excluded"
