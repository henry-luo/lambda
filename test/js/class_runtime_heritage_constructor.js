class RuntimeBase {
  constructor() {
    this.events = { change: 'ready' };
  }
}

const RuntimeMixin = (function (Parent) {
  return class extends Parent {
    constructor(...args) {
      super(...args);
      this.plugins = {};
    }
  };
})(RuntimeBase);

class RuntimeDerived extends RuntimeMixin {
  constructor() {
    super();
  }
}

const mixed = new RuntimeMixin();
console.log(mixed.events.change);
console.log(typeof mixed.plugins);

const derived = new RuntimeDerived();
console.log(derived.events.change);
console.log(typeof derived.plugins);
