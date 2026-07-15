(function () {
  const suffix = 'captured';
  const defaults = { enabled: true };

  class Base {
    constructor(prefix) { this.value = prefix + suffix; }
    describe() { return 'base:' + this.value + ':' + suffix; }
    static get Defaults() { return defaults; }
    static create() { return new this('factory:'); }
  }

  class Child extends Base {
    constructor() { super('child:'); }
    describe() { return 'child>' + super.describe(); }
  }

  class Grandchild extends Child {}

  console.log('static:' + Base.Defaults.enabled);
  console.log(new Child().describe());
  console.log(new Grandchild().describe());
  const constructLater = function () { return new Child().describe(); };
  console.log('callback:' + constructLater());
  const inheritedFactoryLater = function () { return Child.create().describe(); };
  console.log('factory:' + inheritedFactoryLater());
  setTimeout(function () {
    console.log('timer-factory:' + Child.create().describe());
  }, 0);

  const shadowed = function () { return 'outer'; };
  function mutate_captured_let() {
    let shadowed = false;
    const callback = function () { shadowed = true; };
    callback();
    return shadowed;
  }
  console.log('shadow:' + mutate_captured_let() + ':' + shadowed());
})();
