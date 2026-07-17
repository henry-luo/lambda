class e {
  constructor() {
    this.kind = 'wrong';
  }
}

class Renderer {
  constructor(value) {
    this.kind = value;
  }

  initialize() {
    return 'initialized-' + this.kind;
  }
}

function chooseRenderer() {
  var e = { basic: Renderer }.basic;
  var renderer = new e('right');
  console.log(renderer.initialize());
}

chooseRenderer();
