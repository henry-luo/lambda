// S2.4.3v4, S2.4.4v2, S2.4.5v5, S8.2.1v5 and S10.6.1v3.
let source = \.test.lambda.fixtures.'path_node.html'
let doc = source#
let selector = \.rows.1.name
let base = {rows: [{name: "first"}, {name: "second"}]}
let capture = \.file.localhost.missing_path_for_capture
let slash_capture = \.file./.missing_path_for_capture
let fake_host = \.file.'unavailable.invalid'.tmp
let relative_file = \['file'].users
fn read(node, path) => node[path]
fn join_paths(left, right) => left ++ right;
[
    doc is file, doc is element, doc is path, source is path,
    doc.body is <body>, doc[\.body] is <body>, (source ++ \.body)# is <body>,
    read(base, selector) == "second", base[\.rows.2.name] == null,
    base[\] == base, base[capture] == null,
    join_paths("not a path", selector) is error,
    join_paths(null, selector) is error,
    (capture ++ \.child) == \.file.localhost.missing_path_for_capture.child,
    slash_capture == capture,
    string(slash_capture) == "\\.file./.missing_path_for_capture",
    string(capture) == "\\.file.localhost.missing_path_for_capture",
    string(slash_capture ++ \.child) == "\\.file./.missing_path_for_capture.child",
    string((slash_capture)./) == "\\.file./",
    string(\.file./["missing_path_for_capture"]) == string(slash_capture),
    fake_host# is error,
    string(fake_host) == "\\.file.'unavailable.invalid'.tmp",
    string(relative_file) == "\\['file'].users",
    count(\.test.lambda.fixtures?file#) > 0,
    (\.test.lambda.fixtures# is file) == false
];
[slash_capture, capture]
