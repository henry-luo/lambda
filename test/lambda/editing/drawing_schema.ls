// Stage-4 drawing schema validation (mod_drawing_schema + mod_doc_schema).
// Validates Mark drawing trees against the combined doc schema. Outputs are
// booleans / counts (Lambda's printer collapses adjacent bare strings).
import lambda.package.editor.mod_edit_schema
import lambda.package.editor.mod_drawing_schema
import lambda.package.editor.mod_doc_schema

let sch = doc_schema

// 1. schema wiring
"has_drawing:"; sch.drawing.role
"drawing_atomic:"; sch.drawing.atomic
"shape_role:"; sch.shape.role
"layer_role:"; sch.layer.role
"tf_role:"; sch['text-frame'].role
"kinds_count:"; len(shape_kinds)

// 2. a well-formed drawing embedded in a flow doc
let good = <doc;
  <paragraph; "Before.">;
  <drawing id: "D1", width: 400, height: 300;
    <layer id: "L1";
      <shape id: "S1", kind: 'rect', x: 50, y: 50, width: 100, height: 80, fill: "#fff", stroke: "#000">;
      <shape id: "S2", kind: 'ellipse', x: 200, y: 100, width: 120, height: 80>;
      <connector id: "C1", 'from-shape': "S1", 'to-shape': "S2", routing: 'orthogonal'>
    >
  >;
  <paragraph; "After.">
>
"good_valid:"; is_valid(sch, good)
"good_violations:"; len(schema_validate(sch, good))

// 3. group nesting
let grp = <doc; <drawing id: "D5"; <layer id: "L5";
  <group id: "G1"; <shape id: "S5", kind: 'rect'>; <shape id: "S6", kind: 'line'>>>>>
"group_valid:"; is_valid(sch, grp)

// 4. text-frame carrying flow-doc content
let tf = <doc; <drawing id: "D6"; <layer id: "L6";
  <'text-frame' id: "T1", x: 10, y: 10, width: 100, height: 50; <paragraph; "hi">>>>>
"tf_valid:"; is_valid(sch, tf)

// 5. invalid: unknown shape kind
let badkind = <doc; <drawing id: "D2"; <layer id: "L2"; <shape id: "S3", kind: 'blob', x: 0, y: 0>>>>
"badkind_valid:"; is_valid(sch, badkind)
"badkind_msg:"; schema_validate(sch, badkind)[0].message == "attribute validation failed"

// 6. invalid: shape missing required id
let noid = <doc; <drawing id: "D3"; <layer id: "L3"; <shape kind: 'rect', x: 0, y: 0>>>>
"noid_valid:"; is_valid(sch, noid)
"noid_msg:"; schema_validate(sch, noid)[0].message == "required attribute missing"

// 7. invalid: drawing must contain layers (a shape directly is rejected)
let nolayer = <doc; <drawing id: "D4"; <shape id: "S4", kind: 'rect'>>>
"nolayer_valid:"; is_valid(sch, nolayer)

// 8. an empty drawing (no layers) violates layer-plus
let nolayers = <doc; <drawing id: "D7">>
"empty_drawing_valid:"; is_valid(sch, nolayers)
