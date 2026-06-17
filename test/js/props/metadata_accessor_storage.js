function hasOwnName(obj, name) {
  var names = Object.getOwnPropertyNames(obj);
  for (var i = 0; i < names.length; i++) {
    if (names[i] === name) return true;
  }
  return false;
}

var user = {};
user.__get_alpha = 41;
user.__set_alpha = 42;
console.log("user:" + user.__get_alpha + "," + user.__set_alpha);
console.log("userKeys:" + Object.keys(user).join(","));

var proto = {};
Object.defineProperty(proto, "__set_alpha", {
  set: function(v) { this.hit = v + 1; },
  configurable: true
});
var child = Object.create(proto);
child.__set_alpha = 6;
console.log("setter:" + child.hit + "," + child.hasOwnProperty("__set_alpha"));

var obj = {
  get x() { return 3; },
  set x(v) { this.seen = v; },
  get __get_real() { return "gg"; },
  set __set_real(v) { this.stored = v; }
};
obj.x = 9;
obj.__set_real = "ss";
console.log("obj:" + obj.x + "," + obj.seen + "," + obj.__get_real + "," + obj.stored);
console.log("objNames:" +
  hasOwnName(obj, "x") + "," +
  hasOwnName(obj, "__get_real") + "," +
  hasOwnName(obj, "__set_real") + "," +
  hasOwnName(obj, "__get_x") + "," +
  hasOwnName(obj, "__set_x"));

class C {
  get __get_name() { return "cg"; }
  set __set_name(v) { this.v = v; }
  static get __get_static() { return "sg"; }
}
var c = new C();
c.__set_name = 10;
console.log("class:" + c.__get_name + "," + c.v + "," + C.__get_static);
console.log("classNames:" +
  hasOwnName(C.prototype, "__get_name") + "," +
  hasOwnName(C.prototype, "__set_name") + "," +
  hasOwnName(C, "__get_static"));
console.log("OK");
