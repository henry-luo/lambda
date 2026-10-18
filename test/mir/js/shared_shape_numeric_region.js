// D8.4.1v2/D3.4.6: shared descriptors admit native regions without learned slots.
class Point {
  constructor(x, y) { this.x = x; this.y = y; }
  plus(other) { return new Point(this.x + other.x, this.y + other.y); }
}
const a = new Point(1, -0);
const b = new Point(2, -0);
for (let i = 0; i < 5; i++) {
  const sum = a.plus(b);
  if (sum.x !== 3 || 1 / sum.y !== -Infinity) throw 1;
}
a.x = 'x';
if (a.plus(b).x !== 'x2') throw 2;
a.x = 5e-324;
b.x = 0;
if (a.plus(b).x !== 5e-324) throw 3;
a.x = 2;
if (a.plus(b).x !== 2) throw 4;
console.log('shared-shape-numeric-region-ok');
