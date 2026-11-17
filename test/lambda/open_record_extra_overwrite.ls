// Open record admission must preserve values held by constructor null recipes.
type Core = {core:int}
fn normal(value) => {*:value,extra:"same"}
fn admitted(value) Core => value
fn direct(value) Core => {*:value,extra:"same"}
let before={core:1,extra:null};
let formed=normal(before);
let after=admitted(formed);
[before.extra, formed.extra, after.extra, direct(before).extra];
fn numeric_value(value) Core => {*:value,extra:42}
fn nested(value) Core => {*:value,extra:{text:"kept"}}
[numeric_value(before).extra, nested(before).extra.text, before.extra]
let boxed={core:1,extra:error("kept")};
let copied=admitted(boxed);
[(copied.extra ^ { ^.message }) == "kept", (boxed.extra ^ { ^.message }) == "kept"]
