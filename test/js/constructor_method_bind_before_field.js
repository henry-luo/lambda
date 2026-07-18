class OwnMethodBinding {
  constructor() {
    this.dispatch = this.dispatch.bind(this);
  }

  dispatch() {
    return 'own';
  }
}

class BaseMethodBinding {
  dispatch() {
    return 'inherited';
  }
}

class InheritedMethodBinding extends BaseMethodBinding {
  constructor() {
    super();
    this.dispatch = this.dispatch.bind(this);
  }
}

console.log(new OwnMethodBinding().dispatch());
console.log(new InheritedMethodBinding().dispatch());
