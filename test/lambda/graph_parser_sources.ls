// Keep the source boundary stable across graph parser hosts.
let d2 = parse(binary("a.label: \"é😀\"\r\na -> b: edge\r\n")^,
    {type:"graph", flavor:"d2"})^
let d2_bad = parse("@bad\na -> b\n", {type:"graph", flavor:"d2"}) ^ { ^.message }
let dot = parse("strict digraph { \"é\" -> ; c:p -> d [color=red]; }",
    {type:"graph", flavor:"graphviz"})^
let workspace = parse("workspace { model { a = person \"É\"; b = softwareSystem \"S\"; a --uses-> b \"Reads\"; } }",
    {type:"graph", flavor:"c4"})^
let sequence = parse("sequenceDiagram\n actor A as Alice\n A-->>+B: \"<b>x</b>\"\n Note over A,B: see\n",
    {type:"graph", flavor:"mermaid"})^
let tikz = parse("\\node (a) at (1,2) {É}; \\draw (a) -- (0,0);", {type:"tikz"})^
let tikz_bad = parse("\\draw (0,0) to[bend left] (1,1);", {type:"tikz"}) ^ { ^.message }
let tikz_number = parse("\\addplot{1e-5000};", {type:"tikz"})^
let tikz_group = parse("\\addplot{(((1};", {type:"tikz"}) ^ { ^.message }
let file = input("test/input/simple_diagram.d2")^
let explicit_file = input("test/input/simple_diagram.d2", {type:"graph", flavor:"d2"})^
// Stored ZIP member d/a.dot remains a captured input value.
let zip = parse(b'\x504B030414000000000000002100D4767B5A120000001200000007000000642F612E646F7464696772617068207B2061202D3E2062207D504B0102140314000000000000002100D4767B5A1200000012000000070000000000000000000000800100000000642F612E646F74504B0506000000000100010035000000370000000000', "zip")^
let dot_member = content(content(zip)^[0])^[0]
let archive_dot = parse(dot_member, {type:"graph", flavor:"dot"})^;
[d2, d2_bad, dot, workspace, sequence, tikz, tikz_bad, tikz_number, tikz_group,
    format(file, 'mark')^ == format(explicit_file, 'mark')^, archive_dot.flavor]
