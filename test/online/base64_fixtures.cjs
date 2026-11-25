const {scriptResultFixture}=require('./script_result_fixture.cjs');
const everyByte=Buffer.from(Array.from({length:256},(_,i)=>i)).toString('base64');
const json=JSON.stringify({text:'Ā Π 中文 😀',schema_version:'v1'});
const jsonBase64=Buffer.from(json).toString('base64');
const validInputs=[['',''],[' \t\n\r\f',''],['YQ','a'],['YWI','ab'],['YQ==','a'],['YWI=','ab'],[' Y Q = =\f\n\r\t','a'],['YR==','a']];
const cases=[
 ['all byte values retain their character codes and round trip',`
 const expected=${JSON.stringify(everyByte)},decoded=atob(expected);check(decoded.length===256);
 for(let i=0;i<256;i++)check(decoded.charCodeAt(i)===i);
 const bytes=Array.from({length:256},(_,i)=>String.fromCharCode(i)).join('');check(btoa(bytes)===expected&&btoa(decoded)===expected);`],
 ['UTF-8-looking bytes remain distinct binary characters',`
 const decoded=atob('w6k=');check(decoded.length===2&&decoded.charCodeAt(0)===195&&decoded.charCodeAt(1)===169);
 check(btoa('\u00e9')==='6Q=='&&btoa('\u00c3\u00a9')==='w6k=');`],
 ['multilingual JSON survives the SDK byte mapping and UTF-8 decoder',`
 const raw=atob(${JSON.stringify(jsonBase64)});check(raw.length===${Buffer.byteLength(json)});
 const bytes=Uint8Array.from(raw,c=>c.codePointAt(0));
 const decoded=new TextDecoder().decode(bytes);check(decoded===${JSON.stringify(json)}&&JSON.parse(decoded).schema_version==='v1');`],
 ['ASCII whitespace and omitted padding use forgiving base64',`
 for(const [input,expected] of ${JSON.stringify(validInputs)})
  check(atob(input)===expected);check(btoa('')===''&&btoa('abc')==='YWJj');`],
 ['invalid alphabet and padding raise InvalidCharacterError',`
 for(const input of ['YQ=','YQ===','=','==','A','AA\v','-w==','_w==','é','Y=Q=','YQ==A']){
  let rejected=false;try{atob(input)}catch(e){rejected=e.name==='InvalidCharacterError'}check(rejected);
 }`],
 ['btoa rejects non-Latin1 code units',`
 for(const input of ['\u0100',String.fromCharCode(0xd800),String.fromCharCode(0xdc00),String.fromCharCode(0xd83d,0xde00)]){
  let rejected=false;try{btoa(input)}catch(e){rejected=e.name==='InvalidCharacterError'}check(rejected);
 }`],
 ['conversion runs once, retains its value through GC and propagates errors',`
 let calls=0;const encoded=btoa({toString(){calls++;if(typeof gc==='function')gc();return '\u00e9'}});
 check(calls===1&&encoded==='6Q==');calls=0;
 const decoded=atob({toString(){calls++;if(typeof gc==='function')gc();return '/w=='}});check(calls===1&&decoded.charCodeAt(0)===255);
 const reason={tag:'coercion'};for(const convert of [atob,btoa]){
  let preserved=false;try{convert({toString(){throw reason}})}catch(e){preserved=e===reason}check(preserved);
 }`],
];
function fixtureHtml(source){return scriptResultFixture(source,'BASE64_FIXTURE_ERROR','BASE64_FIXTURE_RESULT','<meta charset="utf-8">',true)}
module.exports={cases,fixtureHtml};
