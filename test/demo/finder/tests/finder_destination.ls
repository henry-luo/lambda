import f: ~~.mod_io
import m: ~~.mod_model

let picker = f.read_destination("test/input/grep_tree")^
let code = [for (entry in picker.entries where entry.name == "code") entry][0]
let blocked = f.read_destination(m.parent(code.path),[code.path])^
let nested = f.read_destination(code.path,[code.path])^;
[
    [for (entry in picker.entries) entry.name] == [".hidden_dir","bower_components","code","ignored_dir","venv"],
    len([for (entry in picker.entries where not entry.dir or entry.blocked) entry]) == 0,
    [for (entry in blocked.entries where entry.blocked) entry.path] == [code.path],
    nested.entries[0].name == "deep" and nested.entries[0].blocked,
    f.read_destination(m.child_path(code.path,"deep"))^.entries == [],
    (f.read_destination(m.child_path(code.path,"missing")) ^ { "missing" }) == "missing"
]
