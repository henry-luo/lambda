import normalize: lambda.package.graph.normalize

let valid = <graph direction: "LR", directed: true;
  <subgraph id: "group";
    <node id: "A", width: 120;
      <label format: "markdown"; "**A**">
      <content; <strong; "A">>
    >
    <edge id: "inside", from: "A", to: "A">
  >
>

let invalid = <graph direction: "TB", directed: 7;
  "loose"
  <node id: "A";
    <label format: "xml"; "A">
    <content; "A">
    <content; "duplicate">
    <edge id: "misplaced", from: "A", to: "A">
  >
  <edge id: "missing-to", from: "A">
  <subgraph>
  <unknown>
  <node id: "">
>

let valid_result = normalize.normalize(valid)
let invalid_result = normalize.normalize(invalid)

{
  valid: valid_result.valid,
  valid_diagnostics: valid_result.diagnostics,
  invalid_valid: invalid_result.valid,
  invalid_codes: [for (value in invalid_result.diagnostics) value.code],
  invalid_paths: [for (value in invalid_result.diagnostics) value.path]
}
