class EmitterBase {
  constructor() {
    this.stack = [];
  }
}

class HtmlEmitter extends EmitterBase {
  constructor(options) {
    super();
    this.options = options;
  }
}

var options = { classPrefix: "hljs-" };
var emitter = new HtmlEmitter(options);
console.log(emitter.options.classPrefix);
console.log(emitter.stack.length);
var dynamicEmitter = options.__emitter = HtmlEmitter;
var dynamic = new options.__emitter(options);
console.log(dynamic.options.classPrefix);
console.log(dynamic.stack.length);
class o {
  constructor() {
    this.rootNode = {};
    this.stack = [this.rootNode];
  }
}
class r {
  constructor(e, n) {
    this.classPrefix = n.classPrefix;
  }
}
class l extends o {
  constructor(e) {
    super();
    this.options = e;
  }
  getPrefix() {
    return this.options.classPrefix;
  }
  toHTML() {
    return new r(this, this.options).classPrefix;
  }
}
const p = { classPrefix: "hljs-", __emitter: l };
const S = new p.__emitter(p);
console.log(S.options.classPrefix);
console.log(S.stack.length);
console.log(S.getPrefix());
console.log(S.toHTML());
function nestedConstructorState() {
  class Base {
    constructor() { this.rootNode = {}, this.stack = [this.rootNode]; }
    closeAllNodes() { this.stack = []; }
  }
  class Renderer {
    constructor(owner, options) { this.classPrefix = options.classPrefix; }
  }
  class Emitter extends Base {
    constructor(options) {
      super(), this.options = options;
    }
    toHTML() { return new Renderer(this, this.options).classPrefix; }
    finalize() { return this.closeAllNodes(), true; }
  }
  const options = { classPrefix: "nested-", __emitter: Emitter };
  const emitter = new options.__emitter(options);
  return emitter.finalize(), emitter.toHTML();
}
console.log(nestedConstructorState());
