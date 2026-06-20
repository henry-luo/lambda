import Buffer from 'buffer';

var buf = Buffer.from([1, 2, 3]);

function join(iter) {
    var out = [];
    for (var item of iter) {
        if (Array.isArray(item)) {
            out.push(String(item[0]) + ':' + String(item[1]));
        } else {
            out.push(String(item));
        }
    }
    return out.join(',');
}

console.log(typeof Buffer.prototype.keys);
console.log(typeof Buffer.prototype.values);
console.log(typeof Buffer.prototype.entries);
console.log(Buffer.prototype[Symbol.iterator] === Buffer.prototype.values);
console.log(join(buf));
console.log(join(buf.values()));
console.log(join(buf.keys()));
console.log(join(buf.entries()));

var it = buf.values();
console.log(it[Symbol.iterator]() === it);
console.log(String(it.next().value) + ':' + String(it.next().done));
it.next();
it.next();
console.log(String(it.next().value) + ':' + String(it.next().done));
