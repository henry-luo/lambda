const cases=[
 ['parameter mutations update their owner',`
 const u=new URL('https://example.test/path?b=2&a=x%20y#frag'),p=u.searchParams;
 check(p===u.searchParams&&p.get('a')==='x y');p.set('config',encodeURIComponent('{"x":"a+b c"}'));
 check(u.search.includes('config=')&&new URL(u.href).searchParams.get('config')===encodeURIComponent('{"x":"a+b c"}'));
 p.append('a','z');check(u.href.includes('a=z'));p.delete('b');check(!u.search.includes('b='));
 p.sort();check(u.search.startsWith('?a=x+y&a=z&config=')&&u.hash==='#frag');
 u.search='?x=a#b&space=c d';check(p.get('x')==='a#b'&&p.get('space')==='c d'&&u.hash==='#frag'&&u.search.includes('a%23b'));`],
 ['URL query replacement retains the parameter object',`
 const u=new URL('https://example.test/?old=1'),p=u.searchParams;
 u.search='?new=x%20y&new=z';check(p===u.searchParams&&p.getAll('new').join('|')==='x y|z'&&!p.has('old'));
 u.href='https://other.test:443/new?one=1#part';check(p===u.searchParams&&p.get('one')==='1'&&!p.has('new'));
 check(u.hostname==='other.test'&&u.port===''&&u.pathname==='/new'&&u.hash==='#part');
 p.set('one','two');check(u.toString()==='https://other.test/new?one=two#part');
 const before=u.href;let invalid=false;try{u.href='http://['}catch(error){invalid=error instanceof TypeError}check(invalid&&u.href===before&&p.get('one')==='two');`],
 ['no-op mutations still apply form serialization',`
 const u=new URL('https://example.test/?x=a%20b~');u.searchParams.sort();check(u.href==='https://example.test/?x=a+b%7E');
 const empty=new URL('https://example.test/?#frag');empty.searchParams.delete('absent');check(empty.href==='https://example.test/#frag');
 u.search='';check(u.searchParams.size===0&&u.href==='https://example.test/');`],
 ['public shadows and freezing do not replace URL state',`
 const u=new URL('https://example.test/?x=1');Object.defineProperty(u,'href',{value:'shadow'});
 u.searchParams.set('x','two');check(u.href==='shadow'&&URL.prototype.toString.call(u)==='https://example.test/?x=two');
 const v=new URL('https://example.test/path'),p=v.searchParams;Object.freeze(v);p.append('x','a b');
 check(JSON.stringify(p)==='{}');
 check(v.href==='https://example.test/path?x=a+b'&&v.searchParams===p&&Reflect.ownKeys(p).every(k=>typeof k!=='string'||!k.includes('URL record')));`],
 ['callback collection retains both URL and parameters',`
 const u=new URL('https://example.test/?a=1'),p=u.searchParams;
 p.set({toString(){if(typeof gc==='function')gc();return 'a'}},{toString(){if(typeof gc==='function')gc();return 'b c'}});
 check(u.search==='?a=b+c'&&p.get('a')==='b c');
 u.search={toString(){if(typeof gc==='function')gc();return '?next=2'}};
 check(p.get('next')==='2'&&u.href==='https://example.test/?next=2');`],
 ['Location copies retain URL components',`
 const copied={};for(const key in location)copied[key]=location[key];
 for(const key of ['href','protocol','hostname','port','pathname','search','hash','host','origin'])
 check(Object.hasOwn(copied,key)&&copied[key]===location[key]&&typeof copied[key]==='string');
 check(location!==document&&document.location===location&&window.location===location);
 check(!('nodeType' in location)&&!('document' in location));`],
 ['Location URL attributes are unforgeable own accessors',`
 for(const key of ['href','protocol','hostname','port','pathname','search','hash','host','origin']){
 const d=Object.getOwnPropertyDescriptor(location,key);
 check(!!d&&typeof d.get==='function'&&d.enumerable&&!d.configurable);
 check(key==='origin'?d.set===undefined:typeof d.set==='function');
 check(!Reflect.deleteProperty(location,key));}
 check(location instanceof Location&&Object.prototype.toString.call(location)==='[object Location]');
 check(String(location)===location.href&&location.toString()===location.href);
 let illegal=false;try{new Location()}catch(error){illegal=error instanceof TypeError}check(illegal);`],
 ['captured Location getters validate their receiver',`
 const get=Object.getOwnPropertyDescriptor(location,'pathname').get;
 check(get.call(location)===location.pathname);
 let illegal=false;try{get.call({})}catch(error){illegal=error instanceof TypeError}check(illegal);
 illegal=false;try{location.toString.call(document)}catch(error){illegal=error instanceof TypeError}check(illegal);
 check(document.implementation.createHTMLDocument('').location===null);`],
 ['Location getters remain live across history and fragment writes',`
 const object=location,get=Object.getOwnPropertyDescriptor(object,'hash').get;
 const original=location.href;history.replaceState(null,'','#one');
 check(location===object&&get.call(object)==='#one'&&Object.assign({},object).hash==='#one');
 location.hash='two';check(get.call(object)==='#two'&&location.href.endsWith('#two'));
 history.replaceState(null,'',original);check(location===object&&get.call(object)===new URL(original).hash);`],
 ['empty relative paths resolve against an enumerable Location copy',`
 const base=Object.assign({},location);
 function resolve(path,parent){if(path==='')return parent;return path.charAt(0)==='/'?path:parent.slice(0,parent.lastIndexOf('/')+1)+path}
 const path=resolve('',base.pathname);check(typeof path==='string'&&path.charAt(0)===location.pathname.charAt(0));
 check(resolve('child',base.pathname)===new URL('child',location.href).pathname);`],
 ['Location private ownership survives collection',`
 const object=location,get=Object.getOwnPropertyDescriptor(object,'href').get;
 for(let i=0;i<200;i++)Object.assign({},object);
 if(typeof gc==='function')gc();
 check(document.location===object&&get.call(object)===document.URL);
 check(Reflect.ownKeys(object).every(key=>typeof key!=='string'||!key.includes('Location document')));`],
];
function fixtureHtml(source){return `<!doctype html><style>html,body{margin:0}#result{width:30px;height:30px;background:red}</style><div id=result></div><script>
let okay=true;function check(value){okay=okay&&!!value}try{${source}}catch(error){okay=false;console.log('URL_FIXTURE_ERROR',error.name,error.message)}
if(okay)document.getElementById('result').style.background='lime';</script>`}
module.exports={cases,fixtureHtml};
