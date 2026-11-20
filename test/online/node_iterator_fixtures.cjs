const cases = [
 ['bidirectional root and subtree order', `
 const root=document.createElement('div');root.innerHTML='<i>a</i><b>b</b>';
 const it=document.createNodeIterator(root),nodes=[];let n;
 check(Document.prototype.createNodeIterator.length===1&&NodeIterator.prototype.nextNode.length===0);
 check(it.root===root&&it.referenceNode===root&&it.pointerBeforeReferenceNode&&it.whatToShow===4294967295&&it.filter===null);
 while((n=it.nextNode()))nodes.push(n.nodeName);
 check(nodes.join(',')==='DIV,I,#text,B,#text');
 check(!it.pointerBeforeReferenceNode&&it.previousNode().nodeValue==='b'&&it.pointerBeforeReferenceNode);
 check(it.nextNode().nodeValue==='b'&&it.nextNode()===null);
 const back=[];while((n=it.previousNode()))back.push(n.nodeName);
 check(back.join(',')==='#text,B,#text,I,DIV'&&it.referenceNode===root&&it.pointerBeforeReferenceNode);
 it.detach();check(it.nextNode()===root);`],
 ['filters skip rejected ancestors and retain receivers', `
 const root=document.createElement('div');root.innerHTML='<i>a</i><b>b</b>';
 let calls=0;const filter={acceptNode(n){check(this===filter);calls++;return n.nodeName==='I'?2:1}};
 const it=document.createNodeIterator(root,5,filter),names=[];let n;
 while((n=it.nextNode()))names.push(n.nodeName);
 check(names.join(',')==='DIV,#text,B,#text'&&calls===5&&it.filter===filter);
 let strict=false;const text=document.createNodeIterator(root,4,function(){'use strict';strict=this===undefined;return 65537});
 check(text.nextNode().nodeValue==='a'&&strict);
 check(Object.keys(it).length===0);
 it.__lambda_tree_walker_root=null;it.__lambda_iterator_reference=null;
 it.filter=null;it.root=null;it.whatToShow=0;check(it.filter===filter&&it.root===root&&it.whatToShow===5);`],
 ['removal adjusts both sides of the cursor', `
 const root=document.createElement('div');root.innerHTML='<i id=a><em></em></i><b id=b></b><u></u>';
 const a=root.firstChild,em=a.firstChild,b=a.nextSibling,u=b.nextSibling;
 const it=document.createNodeIterator(root,1);check(it.nextNode()===root&&it.nextNode()===a&&it.nextNode()===em);
 a.remove();check(it.referenceNode===root&&!it.pointerBeforeReferenceNode&&it.nextNode()===b);
 check(it.previousNode()===b&&it.pointerBeforeReferenceNode);b.remove();
 check(it.referenceNode===u&&it.pointerBeforeReferenceNode&&it.nextNode()===u);
 check(it.previousNode()===u);u.remove();check(it.referenceNode===root&&!it.pointerBeforeReferenceNode&&it.nextNode()===null);`],
 ['filter mutation adjusts the in-flight candidate', `
 const root=document.createElement('div');root.innerHTML='<i></i><b></b><u></u>';
 const a=root.firstChild,b=a.nextSibling,u=b.nextSibling;
 const it=document.createNodeIterator(root,1,n=>{if(n===a)n.remove();return 1});
 check(it.nextNode()===root&&it.nextNode()===a&&it.referenceNode===root&&!it.pointerBeforeReferenceNode);
 check(it.nextNode()===b&&it.nextNode()===u);
 const holder=document.createElement('section');holder.appendChild(root);holder.remove();
 check(it.referenceNode===u&&it.previousNode()===u);`],
 ['exceptions and recursive filters preserve usable state', `
 const root=document.createElement('div');root.innerHTML='<i></i>';let fail=true,it;
 it=document.createNodeIterator(root,1,()=>{if(fail){fail=false;throw new Error('filter')}return 1});
 let thrown=false;try{it.nextNode()}catch(e){thrown=e.message==='filter'}
 check(thrown&&it.referenceNode===root&&it.pointerBeforeReferenceNode&&it.nextNode()===root);
 let recursive=false;const re=document.createNodeIterator(root,1,()=>{try{re.nextNode()}catch(e){recursive=e.name==='InvalidStateError'}return 1});
 check(re.nextNode()===root&&recursive);
 let invalid=0;for(const fn of [()=>document.createNodeIterator(null),()=>document.createNodeIterator(root,1,7),()=>NodeIterator.prototype.nextNode.call({})])try{fn()}catch(e){if(e instanceof TypeError)invalid++}
 check(invalid===3&&Object.prototype.toString.call(it)==='[object NodeIterator]');
 let converted=0;try{document.createNodeIterator(root,{valueOf(){converted++;return 1}},7)}catch(e){check(e instanceof TypeError)}
 check(converted===1);`],
 ['cursor ownership survives collection and detached roots', `
 const root=document.createElement('div');root.innerHTML='<i></i><b></b>';
 const it=document.createNodeIterator(root,1,n=>{if(typeof gc==='function')gc();return 1});
 check(it.nextNode()===root&&it.nextNode()===root.firstChild);
 const first=root.firstChild;first.remove();if(typeof gc==='function')gc();
 check(it.referenceNode===root&&it.nextNode()===root.firstChild);
 const outer=document.createElement('section');outer.appendChild(root);root.remove();
 check(it.referenceNode===root.firstChild&&it.previousNode()===root.firstChild);`],
 ['parsed-document sanitizer traversal survives removals', `
 const doc=new DOMParser().parseFromString('<p>keep<script>bad<\/script><em>safe</em></p>','text/html');
 const it=Document.prototype.createNodeIterator.call(doc,doc.body,133),names=[];let n;
 while((n=it.nextNode())){if(n.nodeName==='SCRIPT')n.remove();else names.push(n.nodeName)}
 check(names.join(',')==='BODY,P,#text,EM,#text'&&doc.body.textContent==='keepsafe');
 const text=doc.createTextNode('solo'),single=doc.createNodeIterator(text);check(single.nextNode()===text&&single.nextNode()===null&&single.previousNode()===text);`],
];
function fixtureHtml(source) {
 source=source.replaceAll('</script','<'+String.fromCharCode(92)+'/script');
 return `<!doctype html><style>html,body{margin:0}#result{width:30px;height:30px;background:red}</style><div id=result></div><script>
 let okay=true;function check(value){okay=okay&&!!value}
 try{${source}}catch(e){okay=false;console.log('ITERATOR_FIXTURE_ERROR',e.name,e.message)}
 if(okay)document.getElementById('result').style.background='lime';</script>`;
}
module.exports={cases,fixtureHtml};
