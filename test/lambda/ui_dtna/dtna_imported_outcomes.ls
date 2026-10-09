import c: lambda.ui.core.component
fn mode(node) string^ => c.text(c.option(node.props,"selection",'none'))^
let node = {props:{selection:'multiple'}};
[mode(node)^,mode(node)^ == "multiple",mode(node)^ == "none",
 if (mode(node)^ == "multiple") "yes" else "no",
 c.enum_valid('multiple',["single","multiple"])^,
 c.boolean_props({disabled:1},["disabled"],"test") ^ { "caught" },
 c.text('ready') ^ { "wrong" },c.has({sort:null},"sort"),c.option({sort:null},"sort",'default'),
 (c.node('button',{disabled:null},null,["disabled"]) or null) == null,c.render([0,false,'ready'])]
