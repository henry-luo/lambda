const {scriptResultFixture}=require('./script_result_fixture.cjs');
const cases=[
 ['constructor stacks',`
 for(const C of [Error,TypeError,RangeError,ReferenceError,SyntaxError,URIError,EvalError]){
  for(const construct of [false,true]){
   const error=construct?new C('stack-marker'):C('stack-marker');
   check(error instanceof C&&typeof error.stack==='string');
   check(error.stack.trim().startsWith(C.name+': stack-marker'));
   const d=Object.getOwnPropertyDescriptor(error,'stack');
   check(d&&!d.enumerable&&d.configurable&&(d.writable||typeof d.set==='function'));
  }
 }
 const aggregate=new AggregateError([1],'aggregate');
 check(aggregate.stack.startsWith('AggregateError: aggregate')&&aggregate.errors[0]===1);`],
 ['implicit exception stacks',`
 let caught;try{null.missing()}catch(error){caught=error}
 check(caught instanceof TypeError&&typeof caught.stack==='string'&&caught.stack.trim().startsWith('TypeError'));
 let rejected;try{await Promise.reject(new RangeError('pending'))}catch(error){rejected=error}
 check(rejected.stack.startsWith('RangeError: pending'));`],
 ['subclass stack descriptors',`
 const cause={marker:1};class CustomError extends Error{}
 const error=new CustomError('subclass',{cause});
 check(error instanceof CustomError&&error instanceof Error&&error.cause===cause);
 check(error.stack.trim().startsWith('Error: subclass')&&!Object.keys(error).includes('stack'));
 error.stack='author stack';check(error.stack==='author stack');delete error.stack;check(!Object.hasOwn(error,'stack'));`],
 ['component stack probing survives thrown errors',`
 function probe(component){let baseline;try{throw new Error()}catch(error){baseline=error.stack.trim().split('\\n')}
  try{component()}catch(error){const actual=error.stack.trim().split('\\n');return baseline.length>0&&actual[0]==='Error: component'}return false}
 check(probe(()=>{throw new Error('component')}));`],
 ['lazy subclass names and collection',`
 let calls=0,collections=0;class NamedError extends Error{}
 Object.defineProperty(NamedError.prototype,'name',{get(){calls++;if(typeof gc==='function'){gc();collections++}return 'NamedError'},configurable:true});
 const error=new NamedError('original');check(calls===0);
 const stack=error.stack;check(calls===1&&stack.startsWith('NamedError: original'));
 error.message='later';check(error.stack===stack&&calls===1);
 console.log('ERROR_STACK_COLLECTIONS',collections);`],
];
function fixtureHtml(source){return scriptResultFixture(source,'ERROR_STACK_FIXTURE','ERROR_STACK_RESULT')}
module.exports={cases,fixtureHtml};
