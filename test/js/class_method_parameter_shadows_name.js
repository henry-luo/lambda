class Marker {
  method(Marker) {
    return Marker;
  }
}

const marker = new Marker();
console.log(marker.method('parameter'));

const Alias = class Inner {
  method(Inner) {
    return Inner;
  }
  self() {
    return Inner === Alias;
  }
};

const alias = new Alias();
console.log(alias.method('expression-parameter'));
console.log(alias.self());
