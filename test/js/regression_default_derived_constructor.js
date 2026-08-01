class BaseCodec {
  constructor(value = "base") {
    this.value = value;
  }
}

class CellCodec extends BaseCodec {
  constructor() {
    super("cell");
  }
}

class LegacyCellCodec extends CellCodec {}

console.log(new LegacyCellCodec().value);

const AnonymousCellCodec = class extends BaseCodec {
  constructor() {
    super("anonymous");
  }
};

console.log(new AnonymousCellCodec().value);

var VarCellCodec = class extends BaseCodec {
  constructor() {
    super("var-expression");
  }
};

console.log(new VarCellCodec().value);

var BaseCodecDefault = BaseCodec;
var ExportedCellCodec = class extends BaseCodecDefault {
  constructor() {
    super("export-alias");
  }
};

console.log(new ExportedCellCodec().value);

class ConstructorIdentityBase {
  constructor() {
    console.log(this.constructor.NAME);
  }
}

class ConstructorIdentityDerived extends ConstructorIdentityBase {
  static get NAME() {
    return "derived";
  }
}

new ConstructorIdentityDerived();
