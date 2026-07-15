const n = 'outer';

(function () {
  const n = { value: 'inner' };

  class Base {
    static read() { return n.value; }
  }

  class Child extends Base {}

  console.log('immediate:' + Child.read());
  setTimeout(function () {
    console.log('timer:' + Child.read());
  }, 0);
})();

console.log('outer:' + n);
