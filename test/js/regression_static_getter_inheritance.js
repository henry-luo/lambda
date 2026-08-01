class AbstractComponent {
  static get NAME() {
    throw new Error("derived component must define NAME");
  }
}

class Component extends AbstractComponent {
  constructor() {
    super();
    this.key = this.constructor.DATA_KEY;
  }

  static get DATA_KEY() {
    return "bs." + this.NAME;
  }

  static getInstanceKey() {
    return this.DATA_KEY;
  }

  static create() {
    return new this();
  }
}

class AlertComponent extends Component {
  static get NAME() {
    return "alert";
  }
}

console.log(AlertComponent.DATA_KEY);
console.log(AlertComponent.getInstanceKey());
console.log(new AlertComponent().key);
console.log(AlertComponent.create().key);
