// test decorator desugaring

function sealed(target: any): any {
  console.log("sealed called");
  return target;
}

@sealed
class Greeter {
}

console.log("done");
