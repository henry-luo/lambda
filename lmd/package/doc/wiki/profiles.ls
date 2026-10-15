// Shipped wiki syntax data (D7.2.4); traversal lives in writer.ls.
let base = {heading:"equals",strong:"**",em:"//",code:"{{{",code_end:"}}}",
    block:"braces",links:"brackets",lists:"repeat",indent:2,escape:"\\",table:"pipe",
    underline:null,del:null,sup:null,sub:null,cite:null,br:"\\\\"}
let atlassian = {*:base,heading:"dot",strong:"*",em:"_",code:"{{",code_end:"}}",
    block:"macro",links:"atlassian",table:"atlassian",underline:"+",del:"-",sup:"^",sub:"~",cite:"??"}
pub let names = ["mediawiki","dokuwiki","tikiwiki","twiki","vimwiki","xwiki","zimwiki","creole","jira","confluence"]
pub fn get(flavor) => if (flavor == "mediawiki") {*:base,name:flavor,strong:"'''",em:"''",code:"<code>",code_end:"</code>",block:"mediawiki",table:"mediawiki",br:"<br />",underline:["<u>","</u>"],del:["<del>","</del>"],sup:["<sup>","</sup>"],sub:["<sub>","</sub>"],cite:["<cite>","</cite>"]}
    else if (flavor == "dokuwiki") {*:base,name:flavor,heading:"inverse",code:"''",code_end:"''",block:"doku",lists:"indent",table:"doku",underline:"__"}
    else if (flavor == "tikiwiki") {*:base,name:flavor,heading:"bang",strong:"__",em:"''",code:"-+",code_end:"+-",block:"tiki",links:"tiki",table:"tiki",br:"%%%"}
    else if (flavor == "twiki") {*:base,name:flavor,heading:"plus",strong:"*",em:"_",code:"=",code_end:"=",block:"twiki",links:"twiki",lists:"indent",indent:3,escape:"!",table:"twiki",br:"%BR%"}
    else if (flavor == "vimwiki") {*:base,name:flavor,strong:"*",em:"_",code:"`",code_end:"`",lists:"indent",table:"vim",del:"~~",br:null}
    else if (flavor == "xwiki") {*:base,name:flavor,code:"##",code_end:"##",block:"xwiki",links:"xwiki",escape:"~",underline:"__",del:"--",sup:"^^",sub:",,"}
    else if (flavor == "zimwiki") {*:base,name:flavor,heading:"inverse",code:"''",code_end:"''",block:"zim",lists:"indent",br:"\n",underline:"__",del:"~~"}
    else if (flavor == "creole") {*:base,name:flavor,escape:"~",table:"creole"}
    else if (flavor in ["jira","confluence"]) {*:atlassian,name:flavor}
    else null
