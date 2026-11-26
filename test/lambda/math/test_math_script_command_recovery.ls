// D10.2.2v2: preserve the retired parser's extension arguments and recovery ASTs.
let cases = [
    {source:"\\htmlData{foo bar=green, bar}{x+1}", expected:<math
        <command name:"htmlData", arg0_raw:"foo bar=green, bar", arg1_raw:"x+1",
            <group *[["f","o","o","b","a","r"], <relation value:"=">, ["g","r","e","e","n"],
                <punctuation value:",">, ["b","a","r"]]>
            <group "x" <operator value:"+"> "1">>>},
    {source:"\\class[a]{a b}{}{z}", expected:<math
        <command name:"class", arg0_raw:"a b", arg1_raw:"",
            <brack_group "a"> <group ["a","b"]> <group> <group "z">>>},
    {source:"\\cssId{a-b}{x}", expected:<math
        <command name:"cssId", arg0_raw:"a-b", arg1_raw:"x", <group "a" <operator value:"-"> "b"> <group "x">>>},
    {source:"\\left{(}x+1\\right{)}", expected:<math
        <_seq <ERROR "\\left"> <punctuation value:"(">> "x" <operator value:"+"> "1" <malformed_right delim:")">>},
    {source:"\\left[\\left{(}x\\right{)}\\right]", expected:<math
        <delimiter_group left:"[", right:")", <_seq <ERROR "\\left"> <punctuation value:"(">> "x">
        <malformed_right delim:"]">>},
    {source:"\\left(a\\middle xb\\right)", expected:<math
        <delimiter_group left:"(", right:")", *["a", <middle_delim>, ["x","b"]]>>},
    {source:"\\left(a\\middle\\right)", expected:<math
        <delimiter_group left:"(", right:")", "a" <middle_delim>>>},
    {source:"\\not", expected:<math <command name:"not">>},
    {source:"a\\not", expected:<math "a" <command name:"not">>},
    {source:"\\not{}", expected:<math <not_empty>>}
];
[for (fixture in cases) parse(fixture.source, 'math')^ == fixture.expected]
