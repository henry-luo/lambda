var before = hostobjDemo.destroyed();
var obj = hostobjDemo.create(7);

console.log('ctor type:', typeof hostobjDemo.HostObjDemo);
console.log('value:', obj.value);
obj.value = 10;
console.log('set value:', obj.value);
console.log('label:', obj.label);
console.log('bump:', obj.bump(5));
console.log('after bump:', obj.value);

obj.extra = 'x';
console.log('extra in:', 'extra' in obj);
console.log('extra:', obj.extra);
console.log('keys:', Object.keys(obj).join(','));

var desc = Object.getOwnPropertyDescriptor(obj, 'value');
console.log('desc:', desc.enumerable + ',' + desc.configurable + ',' + desc.writable);
console.log('instanceof:', obj instanceof hostobjDemo.HostObjDemo);
console.log('delete value:', delete obj.value);
console.log('delete extra:', delete obj.extra);
console.log('extra after delete:', 'extra' in obj);

var releaseBefore = hostobjDemo.destroyed();
var doomed = hostobjDemo.create(1);
doomed.extra = { nested: true };
hostobjDemo.release(doomed);
console.log('destroyed grew:', hostobjDemo.destroyed() > releaseBefore);
console.log('released value:', doomed.value === undefined);
gc();
console.log('destroyed stable:', hostobjDemo.destroyed() === releaseBefore + 1);
