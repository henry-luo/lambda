// S2.4.2v6 and S8.2.1v5: names are exact, including empty strings and NUL.
let key = "a\u0000b";
let dynamic = \[key];
let literal = \.'a\u0000b';
let empty = \[""];
let joined = \ ++ key;
let copied = \.parent ++ dynamic;
[
    dynamic != \.a, dynamic == literal, dynamic == joined,
    empty != \, (dynamic).name == key, (copied).name == key,
    string(dynamic) == "\\.'a\\u0000b'",
    string(empty) == "\\.''"
]
