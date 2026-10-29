// S8.2.2v6: XHTML tags retain their expanded namespace identity.
import h: 'http://www.w3.org/1999/xhtml'
// D2.10: XML syntax emits the same HTML names and attributes for Radiant.
let source = "<?xml version=\"1.0\"?><h:html xmlns:h=\"http://www.w3.org/1999/xhtml\"><h:head/><h:body><h:div id=\"box\" class=\"card\"/><h:p> a <![CDATA[<b>&]]> z </h:p></h:body></h:html>"
let doc = parse(source, 'xhtml')^
let html = doc[0]
let body = html[1];
[name(doc) == '#document', name(html) == h.html, name(html[0]) == h.head, name(body) == h.body];
[name(body[0]) == h.div, body[0].id == "box", body[0].class == "card", len(body[0][0]) == 0];
[name(body[1]) == h.p, body[1][0] == " a <b>& z "];
let serialized = format(doc, 'html')^;
[contains(serialized, "<div id=\"box\" class=\"card\""), not contains(serialized, "__lambda_")];
let loaded = input("test/html/xhtml_names.xhtml")^
let explicit = input("test/html/xhtml_names.xhtml", 'xhtml')^;
[name(loaded) == '#document', name(loaded[1]) == h.html, loaded == explicit];
let bad = parse("<html/>", 'xhtml') ^ { true }
let entity = parse("<html xmlns=\"http://www.w3.org/1999/xhtml\">&nbsp;</html>", 'xhtml') ^ { true };
[bad == true, entity == true]
