// Resolve style chains before lowering; direct run formatting can explicitly switch inherited marks off.
import c: .common
import pkg: .package
import theme: .theme
import presentation: .presentation

pub fn run_props(node, colors = null) => {*:map([for (child in c.children(node)
    where contains(["b","i","u","strike","dstrike","vertAlign","caps","smallCaps"],child.tag))
    (child.tag,if (child.tag == "vertAlign") c.attr(child,"val") else c.on(child))]),
    *:presentation.run_props(node,colors)}

pub fn para_props(node, colors = null) {
    let numbering = c.child(node,"numPr")
    let fields = [
        {key:"style_id",node:c.child(node,"pStyle")},
        {key:"outline",node:c.child(node,"outlineLvl")},
        {key:"num_id",node:c.child(numbering,"numId")},
        {key:"level",node:c.child(numbering,"ilvl")}];
    {*:presentation.paragraph_props(node,colors),
        *:map([for (field in fields where field.node != null) (field.key,c.attr(field.node,"val"))]),
        *:if (c.child(node,"pageBreakBefore") == null) {} else {break_before:c.on(c.child(node,"pageBreakBefore"))}}
}
pub fn prepare(package) map^ {
    let root = pkg.related_xml(package,"styles")^
    let numbering = pkg.related_xml(package,"numbering")^
    let colors = theme.load(package)^
    let nodes = c.children(root,"style")
    let invalid = [for (i,node in nodes, let id = c.attr(node,"styleId")
        where id == null or any([for (j,prior in nodes where j < i and c.attr(prior,"styleId") == id) true])) node];
    if ((root != null and (root.tag != "styles" or root.uri != c.W)) or
        (numbering != null and (numbering.tag != "numbering" or numbering.uri != c.W)) or len(invalid) > 0)
        raise c.fail("invalid-package","Invalid styles or numbering part",package.main)
    else {
        let defaults = c.child(root,"docDefaults")
        let default_style = c.first([for (node in nodes where c.attr(node,"type") == "paragraph" and
            contains(["1","true","on"],c.attr(node,"default"))) c.attr(node,"styleId")])
        let default_table = c.first([for (node in nodes where c.attr(node,"type") == "table" and
            contains(["1","true","on"],c.attr(node,"default"))) c.attr(node,"styleId")]);
        {styles:c.dictionary([for (node in nodes) {key:c.attr(node,"styleId"),value:node}]),
            default_style:default_style,default_table:default_table,theme:colors,
            run_defaults:run_props(c.child(c.child(defaults,"rPrDefault"),"rPr"),colors),
            para_defaults:para_props(c.child(c.child(defaults,"pPrDefault"),"pPr"),colors),
            numbering:numbering,part:package.main}
    }
}
fn chain(styles, id, trail = []) map^ {
    if (id == null) {nodes:[],diagnostics:[]}
    else if (contains(trail,id) or len(trail) >= 128) raise c.fail("style-cycle","Cyclic or excessive style inheritance",styles.part)
    else if (styles.styles[id] == null) {nodes:[],diagnostics:[c.issue("missing-style",styles.part,"Unresolved style " ++ id)]}
    else {
        let node = styles.styles[id]
        let base = chain(styles,c.attr(c.child(node,"basedOn"),"val"),trail ++ [id])^;
        {*:base,nodes:base.nodes ++ [node]}
    }
}
fn paragraphs(nodes, index, props, colors) => if (index >= len(nodes)) props else
    paragraphs(nodes,index+1,{*:props,*:para_props(c.child(nodes[index],"pPr"),colors)},colors)

// OOXML toggle properties invert inherited values in styles, but direct formatting overrides.
fn styled_run(nodes, index, props, colors) {
    if (index >= len(nodes)) props else {
        let next = run_props(c.child(nodes[index],"rPr"),colors)
        let changed = map([for (key,value in next)
            (key,if (contains(["b","i","strike","dstrike","caps","smallCaps"],string(key)))
                (if (value) not (props[key] == true) else props[key]) else value)]);
        styled_run(nodes,index+1,{*:props,*:changed},colors)
    }
}
pub fn paragraph(styles, node) map^ {
    let direct = para_props(node,styles.theme)
    let selected = c.get(direct,"style_id",styles.default_style)
    let inherited = chain(styles,selected)^
    let props = {*:paragraphs(inherited.nodes,0,styles.para_defaults,styles.theme),*:direct}
    let style_node = styles.styles[selected];
    // pPr/rPr formats the paragraph mark, not its text runs (OOXML 17.3.1.29).
    {props:props,run:styled_run(inherited.nodes,0,styles.run_defaults,styles.theme),
        style_id:selected,style_name:c.attr(c.child(style_node,"name"),"val"),diagnostics:inherited.diagnostics}
}
pub fn run(styles, base, node) map^ {
    let inherited = chain(styles,c.attr(c.child(node,"rStyle"),"val"))^;
    {props:{*:styled_run(inherited.nodes,0,base,styles.theme),*:run_props(node,styles.theme)},diagnostics:inherited.diagnostics}
}
fn table_properties(nodes, index, props, colors) => if (index >= len(nodes)) props else
    table_properties(nodes,index+1,{*:props,*:presentation.table_props(c.child(nodes[index],"tblPr"),colors)},colors)
pub fn table(styles, node) map^ {
    let selected = c.get(c.child(node,"tblStyle").attrs,c.W ++ "#val",styles.default_table)
    let inherited = chain(styles,selected)^;
    {props:{*:table_properties(inherited.nodes,0,{},styles.theme),*:presentation.table_props(node,styles.theme)},
        sheet:{*:styles,para_defaults:paragraphs(inherited.nodes,0,styles.para_defaults,styles.theme),
            run_defaults:styled_run(inherited.nodes,0,styles.run_defaults,styles.theme)},
        diagnostics:inherited.diagnostics}
}
pub fn list_info(styles, props) {
    let id = props.num_id
    let level = c.as_int(props.level,0)
    let num = c.first([for (n in c.children(styles.numbering,"num") where c.attr(n,"numId") == id) n])
    let abstract_id = c.attr(c.child(num,"abstractNumId"),"val")
    let abstract = c.first([for (n in c.children(styles.numbering,"abstractNum") where c.attr(n,"abstractNumId") == abstract_id) n])
    let override = c.first([for (n in c.children(num,"lvlOverride") where c.as_int(c.attr(n,"ilvl"),-1) == level) n])
    let definition = if (c.child(override,"lvl") != null) c.child(override,"lvl") else
        c.first([for (n in c.children(abstract,"lvl") where c.as_int(c.attr(n,"ilvl"),-1) == level) n])
    let fmt = c.attr(c.child(definition,"numFmt"),"val")
    let start_value = c.get(c.child(override,"startOverride").attrs,c.W ++ "#val",
        c.attr(c.child(definition,"start"),"val"))
    let pattern = c.attr(c.child(definition,"lvlText"),"val")
    let restart = c.as_int(c.attr(c.child(definition,"lvlRestart"),"val"),level);
    if (id == null or id == "0") {value:null,diagnostics:[]}
    else if (num == null or definition == null or level < 0 or level > 8 or
        not contains(["none","bullet","decimal","lowerLetter","upperLetter","lowerRoman","upperRoman"],fmt))
        {value:null,diagnostics:[c.issue("unsupported-numbering",styles.part,"Unresolved or unsupported list numbering")]}
    else {value:{id:id,level:level,format:fmt,start:c.as_int(start_value,1),restart:restart,
            indent:c.pixels(c.attr(c.child(c.child(definition,"pPr"),"ind"),"left"),15.0),
            hanging:c.pixels(c.attr(c.child(c.child(definition,"pPr"),"ind"),"hanging"),15.0)},
        diagnostics:if (fmt != "bullet" and fmt != "none" and pattern != null and
            not contains(["%" ++ string(level+1) ++ ".","%" ++ string(level+1) ++ ")","%" ++ string(level+1)],pattern))
            [c.issue("unsupported-numbering-pattern",styles.part,"Compound list label is rendered as a semantic list")] else []}
}
