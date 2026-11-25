const {scriptResultFixture}=require('./script_result_fixture.cjs');
const htmlNamespace='http://www.w3.org/1999/xhtml';
const fixtures=[
 {name:'HTML names are uppercase',body:`const e=document.createElement('MiXeD');
  return {tag:e.tagName,node:e.nodeName,local:e.localName};`,expected:{tag:'MIXED',node:'MIXED',local:'mixed'}},
 ...['text/xml','application/xml','application/xhtml+xml','image/svg+xml'].map(mime=>({
  name:'XML names retain case: '+mime,body:`const d=new DOMParser().parseFromString('<Root><icon-card/><MiXeD/></Root>',${JSON.stringify(mime)});
   return {root:d.documentElement.tagName,card:d.querySelector('icon-card').tagName,
    node:d.querySelector('MiXeD').nodeName,local:d.querySelector('MiXeD').localName};`,
  expected:{root:'Root',card:'icon-card',node:'MiXeD',local:'MiXeD'}})),
 {name:'foreign namespaces retain case in HTML',body:`const rows=[
  document.createElementNS('http://www.w3.org/2000/svg','linearGradient'),
  document.createElementNS('http://www.w3.org/1998/Math/MathML','mi'),
  document.createElementNS(null,'MiXeD')];return {tags:rows.map(e=>e.tagName),nodes:rows.map(e=>e.nodeName)};`,
  expected:{tags:['linearGradient','mi','MiXeD'],nodes:['linearGradient','mi','MiXeD']}},
 {name:'HTML namespace uses document type',body:`const xml=document.implementation.createDocument(null,'root');
  const inXml=xml.createElementNS('${htmlNamespace}','h:MiXeD'),inHtml=document.createElementNS('${htmlNamespace}','h:MiXeD');
  return {xml:inXml.tagName,xmlNode:inXml.nodeName,html:inHtml.tagName,htmlNode:inHtml.nodeName};`,
  expected:{xml:'h:MiXeD',xmlNode:'h:MiXeD',html:'H:MIXED',htmlNode:'H:MIXED'}},
 {name:'adoption changes HTML uppercase policy',body:`const xml=document.implementation.createDocument(null,'root'),e=xml.createElementNS('${htmlNamespace}','h:MiXeD');
  const before=e.tagName;document.adoptNode(e);const after=e.nodeName;xml.adoptNode(e);
  return {before,after,returned:e.tagName};`,expected:{before:'h:MiXeD',after:'H:MIXED',returned:'h:MiXeD'}},
 {name:'XML card dispatch preserves authored names',body:`const xml=new DOMParser().parseFromString('<cards><icon-card image="icon.svg">Service</icon-card><simple-card><title>Guide</title></simple-card></cards>','text/xml');
  const cards=Array.from(xml.querySelectorAll('icon-card,simple-card')).map(e=>{
   switch(e.tagName){case 'icon-card':return {type:e.tagName,title:e.textContent};
    case 'simple-card':return {type:e.tagName,title:e.querySelector('title').textContent}}});return {cards};`,
  expected:{cards:[{type:'icon-card',title:'Service'},{type:'simple-card',title:'Guide'}]}},
];
function fixtureHtml(f){return scriptResultFixture(`const result=(function(){${f.body}})();
 console.log('ELEMENT_NAME_DETAILS',JSON.stringify(result));check(JSON.stringify(result)===${JSON.stringify(JSON.stringify(f.expected))});`,
 'ELEMENT_NAME_ERROR','ELEMENT_NAME_RESULT');}
module.exports={fixtures,fixtureHtml};
