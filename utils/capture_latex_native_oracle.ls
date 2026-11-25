// Capture before the native provider is removed; TeX source is always data (S1.8).
let cases=[
 {kind:"tex",ini:false,source:"Hello"},
 {kind:"tex",ini:false,source:"\\def\\x{World}Hello \\x"},
 {kind:"tex",ini:false,source:"\\message{hello}Text"},
 {kind:"tex",ini:false,source:"\\iftrue A\\else B\\fi"},
 {kind:"tex",ini:false,source:"\\ifnum1 1 T\\else F\\fi X"},
 {kind:"tex",ini:false,source:"\\message"},
 {kind:"tex",ini:true,source:"\\message{hello}Text"},
 {kind:"tex",ini:true,source:"\\count0=42 \\the\\count0"},
 {kind:"tex",ini:true,source:"\\ifnum2>1 Yes\\else No\\fi"},
 {kind:"latex",ini:false,source:"\\begin{document}Hello\\end{document}"},
 {kind:"latex",ini:false,source:"\\begin{document}A \\textbf{bold} B\\end{document}"},
 {kind:"latex",ini:false,source:"\\begin{document}$x+1$\\end{document}"},
 {kind:"latex",ini:false,source:"\\def\\x{Hi}\\begin{document}\\x\\end{document}"},
 {kind:"tex",ini:false,source:"Hello\n"}
];
let captured=[for(item in cases) {*:item,
    expected:format(parse(item.source,{type:item.kind,ini:item.ini})^,'json')^}];
format(captured,'json')^
