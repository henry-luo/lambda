import compare: ._tex_script_recovery

let sources=[
 "A\\ignorespaces   B",
 "A\\ignorespaces\n  B",
 "\\def\\s{ }A\\ignorespaces\\s\\s B",
 "\\def\\s{ }\\def\\b{ B}A\\ignorespaces\\s\\b",
 "\\def\\s{ }A\\ignorespaces\\s{ B}",
 "\\def\\s{ }A\\ignorespaces\\s\\relax B",
 "\\def\\s{ }A\\ignorespaces\\iftrue\\s\\else X\\fi B",
 "\\def\\s{ }A\\ignorespaces\\s",
 "\\def\\s{ }A\\ignorespaces\\noexpand\\s B",
 "\\def\\s{X}A\\ignorespaces\\noexpand\\s B",
 "\\def\\s{X}A\\ignorespaces\\expandafter\\noexpand\\s B",
 "A{\\ignorespaces  B}C",
 "A\\let\\skipspaces\\ignorespaces\\skipspaces B"
];
let checked=[for(source in sources, mode in [false,true]){source:source,mode:mode,
 result:compare.check(source,{constructor_argument_mode:mode})^}];
{cases:len(checked),failures:[for(item in checked where item.result.issue!=null or
 not item.result.text or not item.result.messages or not item.result.diagnostics)item]}
