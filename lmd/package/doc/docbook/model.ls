import xml: lambda.doc.xml
import c: .common
import ids: .ids

fn adapt(value,scope,context,key,depth = 0) any^ {
    if (value is string) value
    else if (not (value is element)) raise c.fail("invalid-source","XML content must be text or an element")
    else if (depth > 256) raise c.fail("invalid-source","Document nesting exceeds 256 levels")
    else {
        let spelling = string(name(value))
        let bindings = xml.bindings(value,scope)
        let expanded_name = if (starts_with(spelling,"?") or starts_with(spelling,"!"))
            {local:spelling,uri:""} else xml.expanded(spelling,bindings)
        let attrs = xml.attributes(value,bindings)
        let lang = c.get(attrs,c.XML ++ "#lang",context.language)
        let base = attrs[c.XML ++ "#base"]
        let next = {*:context,language:lang,base_uri:if (base == null) context.base_uri
            else c.resolve_uri(context.base_uri,base)};
        if (expanded_name.uri == null or any([for (attr_key,attr_value in map(value) where
                string(attr_key) != "xmlns" and not starts_with(string(attr_key),"xmlns:"))
                xml.expanded(attr_key,bindings,true).uri == null]))
            raise c.fail("invalid-namespace","Unbound XML prefix",context.source_uri)
        else {tag:expanded_name.local,uri:expanded_name.uri,attrs:attrs,key:key,source_uri:context.source_uri,
            base_uri:next.base_uri,language:lang,space:c.get(attrs,c.XML ++ "#space",context.space),
            raw:value,children:[for (i,child in content(value))
                adapt(child,bindings,{*:next,space:c.get(attrs,c.XML ++ "#space",context.space)},
                    key ++ "/" ++ string(i+1),depth+1)^]}
    }
}

pub fn prepare(source) map^ {
    let snapshot = if (source is map and source.document != null) source
        else {document:source,source_uri:null,base_uri:".",resources:{}}
    let document = xml.root(snapshot.document);
    if (not (document is element)) raise c.fail("invalid-source","Expected a parsed DocBook XML document")
    else if (snapshot.resources != null and not (snapshot.resources is map))
        raise c.fail("invalid-source","Snapshot resources must be a map")
    else if ((snapshot.base_uri != null and not (snapshot.base_uri is string)) or
        (snapshot.source_uri != null and not (snapshot.source_uri is string)))
        raise c.fail("invalid-source","Snapshot source/base URIs must be strings")
    else {
        let root = adapt(document,{}, {source_uri:snapshot.source_uri,
            base_uri:c.get(snapshot,"base_uri","./"),language:"en",space:"default"},"1")^
        let flat = c.nodes(root)
        let ids = [for (node in flat,let id = c.attr(node,"id",c.XML) where id != null) {key:id,value:node}]
        let duplicates = [for (i,entry in ids where entry.key == "" or
            any([for (j,prior in ids where j < i) prior.key == entry.key])) entry.key];
        if (root.uri != c.DB or not contains(["article","book","set","part","reference"],root.tag))
            raise c.fail("invalid-source","Expected a DocBook 5.x article, book, set, part or reference root",snapshot.source_uri)
        else if (len(duplicates) > 0) raise c.fail("duplicate-id","Duplicate or empty xml:id: " ++ duplicates[0],snapshot.source_uri)
        else {root:root,nodes:flat,ids:c.dictionary(ids),resources:c.get(snapshot,"resources",{}),
            source_uri:snapshot.source_uri,base_uri:root.base_uri}
    }
}

fn available_id(candidate,source_ids) => if (candidate in source_ids)
    available_id(candidate ++ "-generated",source_ids) else candidate

fn analyzed(node,ancestors,siblings,index,options) {
    let prior = take(siblings,index)
    let parent = c.first(reverse(ancestors))
    let section = c.is_section(node)
    let ordinal = if (node.tag == "chapter") string(len([for (n in prior where n.tag == "chapter") n])+1)
        else if (node.tag == "appendix") chr(65+len([for (n in prior where n.tag == "appendix") n]))
        else if (section and node.tag != "simplesect")
            // xslTNG 2.8.5 uses sections-inherit-from; the older *-numbers-inherit parameters are inert.
            (if (parent.number != null and c.is_section(parent) and
                options.parameters["sections-inherit-from"] == "section") parent.number ++ "." else "") ++
                string(len([for (n in prior where c.is_section(n)) n])+1)
        else null
    let numbered = if (section) options.parameters["section-numbers"] and
        not any([for (p in ancestors) p.tag == "preface"]) else options.parameters["component-numbers"]
    let generated = if (parent == null) options.parameters["generated-id-root"] else
        parent.id ++ options.parameters["generated-id-sep"] ++ c.get(ids.ABBREVIATIONS,node.tag,node.tag) ++
            string(len([for (entry in prior where entry.uri == node.uri and entry.tag == node.tag) entry])+1)
    let id = c.attr(node,"id",c.XML)
    let resolved = if (id != null) id else available_id(generated,options.source_ids)
    let current = {*:node,id:resolved,explicit_id:id != null,number:if (numbered) ordinal else null,
        ancestors:[for (ancestor in ancestors) ancestor.key],parent:parent.key,
        heading:if (section) min(5,len([for (p in ancestors where c.is_section(p)) p])+2)
            else if (node.tag in ["chapter","appendix","preface"] and parent.tag in ["book","article"]) 2 else 1,
        division:if (contains(["chapter","appendix","preface","article"],node.tag)) node.key else parent.division}
    let elements = [for (child in node.children where child is map) child];
    {*:current,children:[for (child in node.children)
        if (not (child is map)) child else analyzed(child,[*ancestors,current],elements,
            c.first([for (i,entry in elements where entry.key == child.key) i]),options)]}
}

pub fn analyze(source,options) {
    let ids = [for (key,value in source.ids) string(key)]
    let root = analyzed(source.root,[],[source.root],0,{*:options,source_ids:ids})
    let nodes = c.nodes(root)
    let notes = [for (node in nodes where node.uri == c.DB and node.tag == "footnote") node]
    let by_key = c.dictionary([for (node in nodes) {key:node.key,value:node}])
    let objects = [for (node in nodes where node.uri == c.DB and node.tag in ["figure","table","example","equation","procedure"]) node];
    {*:source,root:root,nodes:nodes,
        by_key:by_key,
        ids:c.dictionary([for (node in nodes where node.explicit_id) {key:node.id,value:node}]),
        notes:c.dictionary([for (i,node in notes) {key:node.key,value:i+1}]),
        formal_numbers:c.dictionary([for (i,node in objects,let parent = by_key[node.division])
            {key:node.key,value:(if (parent.number == null) "" else parent.number ++ ".") ++
                string(len([for (prior in take(objects,i) where prior.tag == node.tag and prior.division == node.division) prior])+1)}])}
}
