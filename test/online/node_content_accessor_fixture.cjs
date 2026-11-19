function source() {
    return `<!doctype html><style>body{margin:0;background:white}#status{width:100px;height:100px;background:red}</style>
<div id=status></div><script>try{
const descriptors={};
for(const name of ['nodeValue','textContent']){
    const d=Object.getOwnPropertyDescriptor(Node.prototype,name);
    if(!d||typeof d.get!=='function'||typeof d.set!=='function')throw Error('missing Node accessor '+name);
    if(!d.enumerable||!d.configurable)throw Error('wrong descriptor flags '+name);
    descriptors[name]=d;
}
let checks=0;
function check(actual,expected){if(actual!==expected)throw Error('check '+checks+': '+actual+' !== '+expected);checks++}
const value=descriptors.nodeValue,content=descriptors.textContent;
for(const node of [document.createTextNode('text'),document.createComment('text'),document.createAttribute('data-probe')]){
    value.set.call(node,'first');check(value.get.call(node),'first');check(content.get.call(node),'first');
    content.set.call(node,42);check(value.get.call(node),'42');
    value.set.call(node,null);check(content.get.call(node),'');
}
for(const node of [document.createElement('div'),document.createDocumentFragment()]){
    node.appendChild(document.createTextNode('left'));
    const child=document.createElement('b');child.textContent='right';node.appendChild(child);
    check(value.get.call(node),null);value.set.call(node,'ignored');check(content.get.call(node),'leftright');
    const observer=new MutationObserver(()=>{});observer.observe(node,{childList:true});
    content.set.call(node,23);check(content.get.call(node),'23');check(node.childNodes.length,1);
    check(observer.takeRecords().length,1);observer.disconnect();
    content.set.call(node,null);check(node.childNodes.length,0);
}
for(const node of [document,document.doctype]){
    check(value.get.call(node),null);check(content.get.call(node),null);
    value.set.call(node,'ignored');content.set.call(node,'ignored');
    check(content.get.call(node),null);
}
for(const name of ['nodeValue','textContent'])for(const receiver of [{},null]){
    const d=descriptors[name];
    for(const operation of [()=>d.get.call(receiver),()=>d.set.call(receiver,'x')]){
        let branded=false;try{operation()}catch(e){branded=e instanceof TypeError}check(branded,true);
    }
}
document.getElementById('status').style.background='lime';
console.log('NODE_CONTENT_ACCESSORS',checks);
}catch(error){console.log('NODE_CONTENT_FAILURE',error.message);throw error}
</script>`;
}
function detachedSource(){
    return `<!doctype html><style>body{margin:0;background:white}</style><body><script>
for(const owner of [document,document.implementation.createHTMLDocument('foreign')]){
    const text=owner.createTextNode('before');
    const observer=new MutationObserver(()=>{});observer.observe(text,{characterData:true,characterDataOldValue:true});
    text.data='after';text.replaceData(1,3,'BCD');
    const records=observer.takeRecords();observer.disconnect();
    console.log('DETACHED_TEXT',text.data,JSON.stringify(records.map(r=>r.oldValue)),text.ownerDocument===owner);
    if(text.data!=='aBCDr'||records.length!==2||records[0].oldValue!=='before'||records[1].oldValue!=='after')throw Error('detached text mutation lost');
}
document.body.style.background='lime';console.log('DETACHED_TEXT_DONE');
</script>`;
}
module.exports={source,detachedSource};
