function Node(link, id, queue) {
  this.link = link;
  this.id = id;
  this.queue = queue;
  this.state = queue == null ? 2 : 3;
}

var owner = {};
var a = new Node(null, 0, null);
a.scheduler = owner;
var b = new Node(a, 1, {});
b.scheduler = owner;
var c = new Node(b, 2, {});
c.scheduler = owner;

console.log(a.id + ":" + (a.link == null));
console.log(b.id + ":" + (b.link == null ? "null" : b.link.id));
console.log(c.id + ":" + (c.link == null ? "null" : c.link.id) + ":" +
    (c.link && c.link.link ? c.link.link.id : "null"));
