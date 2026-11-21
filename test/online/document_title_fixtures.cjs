const {scriptResultFixture}=require('./script_result_fixture.cjs');
const cases=[
 ['strict title writes update the title node',`'use strict';
 const descriptor=Object.getOwnPropertyDescriptor(Document.prototype,'title');check(typeof descriptor.set==='function');
 let conversions=0;const input={toString(){conversions++;if(typeof gc==='function')gc();return '  new\\n  title  '}};
 document.title=input;check(conversions===1&&document.title==='new title');
 check(document.querySelector('title').textContent==='  new\\n  title  ');
 document.querySelector('title').textContent='  edited\\t node  ';check(document.title==='edited node');`],
 ['title creation and document isolation',`'use strict';
 const original=document.title,a=document.implementation.createHTMLDocument('alpha'),b=document.implementation.createHTMLDocument('beta');
 a.querySelector('title').remove();a.title='created';b.title='second';
 check(a.head.querySelector('title').textContent==='created'&&a.title==='created'&&b.title==='second'&&document.title===original);
 a.head.remove();a.querySelector('title')?.remove();a.title='ignored';check(a.title==='');`],
 ['title uses the first HTML title in tree order',`'use strict';
 const doc=document.implementation.createHTMLDocument('first'),second=doc.createElement('title');second.textContent='second';doc.body.append(second);
 doc.title='updated';check(doc.querySelectorAll('title')[0].textContent==='updated'&&second.textContent==='second');
 doc.head.remove();doc.title='body title';check(second.textContent==='body title'&&doc.title==='body title');`],
 ['title descriptors keep readonly document fields readonly',`'use strict';
 let rejected=false;try{document.URL='replacement'}catch(error){rejected=error instanceof TypeError}check(rejected);
 const descriptor=Object.getOwnPropertyDescriptor(Document.prototype,'title');
 let branded=false;try{descriptor.set.call({},'bad')}catch(error){branded=error instanceof TypeError}check(branded);
 descriptor.set.call(document,42);check(descriptor.get.call(document)==='42'&&document.querySelector('title').textContent==='42');`],
 ['SVG title uses a direct SVG title child',`'use strict';
 const ns='http://www.w3.org/2000/svg',doc=document.implementation.createDocument(ns,'svg',null);
 const group=doc.createElementNS(ns,'g'),nested=doc.createElementNS(ns,'title');nested.textContent='nested';group.append(nested);doc.documentElement.append(group);
 doc.title='  SVG\\t title ';check(doc.title==='SVG title'&&nested.textContent==='nested');
 check(doc.documentElement.firstChild.localName==='title'&&doc.documentElement.firstChild.namespaceURI===ns);`],
];
function fixtureHtml(source){return scriptResultFixture(source,'DOCUMENT_TITLE_FIXTURE','DOCUMENT_TITLE_RESULT','<title>initial</title>',true)}
module.exports={cases,fixtureHtml};
