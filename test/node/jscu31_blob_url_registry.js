const { Blob, resolveObjectURL } = require('buffer');
const { URL } = require('url');

const ids = [];
for (let i = 0; i < 1025; i++) {
    ids.push(URL.createObjectURL(new Blob(['x'])));
}

gc();
console.log(resolveObjectURL(ids[0]).size);
console.log(resolveObjectURL(ids[1024]).size);
URL.revokeObjectURL(ids[0]);
console.log(resolveObjectURL(ids[0]) === undefined);
console.log(resolveObjectURL(ids[1024]).size);
