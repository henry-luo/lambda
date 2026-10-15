// Port of the default chunk predicates and filenames in xslTNG modules/chunk.xsl.
import c: .common

fn siblings(node,model) => if (node.parent == null) [model.root] else c.children(model.by_key[node.parent])
fn ordinal(node,model) => c.first([for (i,entry in [for (peer in siblings(node,model)
    where peer.tag == node.tag) peer] where entry.key == node.key) i])+1
fn padded(value,width) => if (len(string(value)) >= width) string(value)
    else join([for (i in 1 to (width-len(string(value)))) "0"],"") ++ string(value)
fn included(node,model,options) {
    let parent = model.by_key[node.parent]
    let eligible = parent.tag in ["set","book","part","reference"] or
        node.tag in ["refentry","section","sect1"]
    let prior = [for (peer in siblings(node,model) where peer.tag == node.tag) peer]
    let first_section = node.tag in ["section","sect1"] and prior[0].key == node.key
    let in_partintro = node.tag == "partintro" or any([for (key in node.ancestors) model.by_key[key].tag == "partintro"])
    let depth = len([for (key in node.ancestors where model.by_key[key].tag == "section") key]);
    eligible and not first_section and not in_partintro and not (node.tag in ["annotation","toc","info"]) and
        (not c.is_section(node) or depth < options.parameters["chunk-section-depth"])
}
fn basename(node,model) {
    if (node == null) "" else {
        let prefixes = {acknowledgements:"ack",chapter:"ch",colophon:"col",dedication:"ded",preface:"pf",
            article:"ar",bibliography:"bib",glossary:"gloss",index:"idx",reference:"ref",refentry:"mp",topic:"top"}
        let parent = model.by_key[node.parent]
        let index = ordinal(node,model);
        if (node.tag in ["book","set"] and len(siblings(node,model)) <= 1) ""
        else if (node.tag == "book") basename(parent,model) ++ "bk" ++ padded(index,2)
        else if (node.tag == "set") "set" ++ padded(index,2)
        else if (node.tag == "appendix") basename(parent,model) ++ "app" ++ chr(96+index)
        else if (c.is_section(node)) basename(parent,model) ++ "s" ++ padded(index,2)
        else if (prefixes[node.tag] != null) basename(parent,model) ++ prefixes[node.tag] ++
            padded(index,if (node.tag in ["refentry","topic"]) 3 else 2)
        else node.id
    }
}
pub fn plan(model,options) map^ {
    let roots = [model.root,*if (options.output == "single") [] else
        [for (node in model.nodes where node.key != model.root.key and c.is_structure(node) and included(node,model,options)) node]]
    let pages = [for (i,node in roots) {node:node,path:if (i == 0) "index.html" else
        (if (options.parameters["use-id-as-filename"] and node.explicit_id) node.id else basename(node,model)) ++ ".html"}]
    let root_paths = c.dictionary([for (page in pages) {key:page.node.key,value:page.path}])
    let owners = c.dictionary([for (node in model.nodes) {key:node.key,
        value:root_paths[c.first([for (key in reverse([*node.ancestors,node.key]) where root_paths[key] != null) key])]}]);
    if (len(unique([for (page in pages) page.path])) != len(pages))
        raise c.fail("invalid-path","Chunk filenames collide")
    else {pages:pages,roots:root_paths,owners:owners}
}
pub fn navigation(plan,index) {
    let page = plan.pages[index]
    let ancestor = c.first([for (key in reverse(page.node.ancestors) where plan.roots[key] != null) key]);
    {previous:if (index == 0) null else plan.pages[index-1],
        next:if (index+1 >= len(plan.pages)) null else plan.pages[index+1],
        up:c.first([for (entry in plan.pages where entry.node.key == ancestor) entry]),
        home:if (index == 0) null else plan.pages[0]}
}
