const parent = document.getElementById('parent');
const target = document.getElementById('target');
const child = document.getElementById('child');
console.log(target.getAnimations().length === 0);
const first = target.animate([{opacity:0},{opacity:1}], {duration:1000});
first.pause(); first.currentTime = 100;
console.log(target.getAnimations().length === 1 && target.getAnimations()[0] === first);
const second = target.animate([{opacity:1},{opacity:0}], {duration:1000});
console.log(target.getAnimations().map(animation => animation === first ? 1 : 2).join(',') === '1,2');
console.log(target.getAnimations() !== target.getAnimations());
console.log(parent.getAnimations().length === 0 && parent.getAnimations({subtree:true}).length === 2);
const nested = child.animate([{opacity:0},{opacity:1}], {duration:1000});
console.log(parent.getAnimations({subtree:true}).length === 3);
first.cancel();
console.log(first.currentTime === null && target.getAnimations().length === 1 && target.getAnimations()[0] === second);
const third = target.animate([{opacity:0},{opacity:1}], {duration:1000});
console.log(target.getAnimations()[0] === second && target.getAnimations()[1] === third);
first.currentTime = 200;
console.log(target.getAnimations()[0] === first && target.getAnimations()[1] === second && target.getAnimations()[2] === third);
Object.freeze(second); second.cancel();
console.log(target.getAnimations().length === 2);
console.log(first.__lambda_web_animation_host === undefined && Object.getOwnPropertyNames(first).every(name => !name.includes('host')));
const get = Element.prototype.getAnimations;
console.log(get.length === 0);
function rejects(receiver) {try {get.call(receiver); return false;} catch(error) {return error instanceof TypeError;}}
console.log([{}, null, undefined, document, new Proxy(target,{})].every(rejects));
const token = {};
try {get.call(target,{get subtree(){throw token;}}); console.log(false);}
catch(error){console.log(error === token);}
console.log(Object.prototype.toString.call(first) === '[object Animation]');
for (const animation of parent.getAnimations({subtree:true})) animation.cancel();
console.log(parent.getAnimations({subtree:true}).length === 0);
