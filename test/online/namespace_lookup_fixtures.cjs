const fixtures = [
    {name:'captured Node namespace lookup supports consent wrapper calls', body:`
        const lookup=Node.prototype.lookupNamespaceURI;
        const call=Function.prototype.call.bind(Function.prototype.call);
        const locate=node=>node?call(lookup,node,null):null;
        const div=document.createElement('div'),svg=document.createElementNS('http://www.w3.org/2000/svg','svg');
        const text=document.createTextNode('text');svg.appendChild(text);
        return {html:locate(div)==='http://www.w3.org/1999/xhtml',svg:locate(svg)==='http://www.w3.org/2000/svg',text:locate(text)==='http://www.w3.org/2000/svg',document:locate(document)==='http://www.w3.org/1999/xhtml',empty:lookup.call(div,'')===locate(div)};
    `, expected:{html:true,svg:true,text:true,document:true,empty:true}},
    {name:'namespace lookup honors expanded names, declarations and live shadowing', body:`
        const xmlns='http://www.w3.org/2000/xmlns/';
        const outer=document.createElement('div'),child=document.createElement('span');outer.appendChild(child);
        outer.setAttribute('xmlns:q','urn:literal');
        outer.setAttributeNS(xmlns,'xmlns:p','urn:outer');
        const inherited=child.lookupNamespaceURI('p')==='urn:outer';
        child.setAttributeNS(xmlns,'xmlns:p','urn:inner');
        const shadowed=child.lookupNamespaceURI('p')==='urn:inner';
        child.removeAttributeNS(xmlns,'p');
        const restored=child.lookupNamespaceURI('p')==='urn:outer';
        const named=document.createElementNS('urn:actual','p:item');outer.appendChild(named);
        named.setAttributeNS(xmlns,'xmlns:p','urn:declaration');
        const own=named.lookupNamespaceURI('p')==='urn:actual';
        const blank=document.createElementNS(null,'blank');outer.appendChild(blank);blank.setAttributeNS(xmlns,'xmlns','');
        return {inherited,shadowed,restored,own,reset:blank.lookupNamespaceURI(null)===null,reserved:child.lookupNamespaceURI('xml')==='http://www.w3.org/XML/1998/namespace',unbound:child.lookupNamespaceURI('missing')===null,literal:child.lookupNamespaceURI('q')===null};
    `, expected:{inherited:true,shadowed:true,restored:true,own:true,reset:true,reserved:true,unbound:true,literal:true}},
    {name:'namespace lookup dispatches attributes, fragments and XML documents', body:`
        const root=document.createElementNS('urn:owner','p:owner');root.setAttribute('data-x','value');
        const attr=root.getAttributeNode('data-x'),comment=document.createComment('note');root.appendChild(comment);
        const attached=attr.lookupNamespaceURI('p')==='urn:owner'&&comment.lookupNamespaceURI('p')==='urn:owner';
        root.removeAttributeNode(attr);
        const fragment=document.createDocumentFragment(),text=document.createTextNode('x'),bare=document.createElementNS(null,'bare');fragment.appendChild(text);fragment.appendChild(bare);
        const isolated=fragment.lookupNamespaceURI('xml')===null&&text.lookupNamespaceURI(null)===null&&bare.lookupNamespaceURI(null)===null&&document.doctype.lookupNamespaceURI('xml')===null;
        const xml=new DOMParser().parseFromString('<r xmlns="urn:base"><p:child xmlns:p="urn:child"/></r>','application/xml');
        return {attached,detached:attr.lookupNamespaceURI('p')===null,isolated,xml:xml.lookupNamespaceURI(null)==='urn:base'&&xml.documentElement.firstChild.lookupNamespaceURI('p')==='urn:child'};
    `, expected:{attached:true,detached:true,isolated:true,xml:true}},
    {name:'namespace lookup preserves WebIDL method branding and conversion', body:`
        const lookup=Node.prototype.lookupNamespaceURI,descriptor=Object.getOwnPropertyDescriptor(Node.prototype,'lookupNamespaceURI');
        const element=document.createElement('div');let count=0;
        const converted=lookup.call(element,{toString(){count++;return 'xml';}});
        let symbol=false,brand=false;
        try{lookup.call(element,Symbol('prefix'));}catch(error){symbol=error instanceof TypeError;}
        try{lookup.call({},null);}catch(error){brand=error instanceof TypeError;}
        return {descriptor:lookup.length===1&&descriptor.enumerable&&descriptor.configurable&&descriptor.writable,converted:count===1&&converted==='http://www.w3.org/XML/1998/namespace',nullable:lookup.call(element,undefined)===lookup.call(element,null),symbol,brand};
    `, expected:{descriptor:true,converted:true,nullable:true,symbol:true,brand:true}}
];
function fixtureHtml(f) {
    return `<!doctype html><div>Namespace lookup</div><script>
    let result;try{result=(function(){${f.body}})();}catch(error){result={error:String(error)};}
    console.log('NAMESPACE_LOOKUP_RESULT',JSON.stringify(result));</script>`;
}
module.exports={fixtures,fixtureHtml};
