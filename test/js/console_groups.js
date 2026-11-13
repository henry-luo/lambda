console.log(typeof console.group, typeof console.groupCollapsed, typeof console.groupEnd);
console.group('outer', 7);
console.groupCollapsed({toString() { return 'inner'; }});
console.groupEnd();
console.groupEnd();
console.groupEnd();
console.log('groups completed');
