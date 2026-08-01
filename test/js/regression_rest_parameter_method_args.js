class PointLike {
  constructor(x = 0, y = 0) {
    this._x = 0;
    this._y = 0;
    this.x = x;
    this.y = y;
  }

  get x() { return this._x; }
  set x(x) {
    if (Number.isNaN(x)) throw new Error("Invalid x supplied.");
    this._x = x;
  }
  get y() { return this._y; }
  set y(y) {
    if (Number.isNaN(y)) throw new Error("Invalid y supplied.");
    this._y = y;
  }
}

class RectangleLike extends PointLike {
  constructor(x = 0, y = 0, width = 0, height = 0) {
    super(x, y);
    this._width = 0;
    this._height = 0;
    this.width = width;
    this.height = height;
  }
}

class GeometryLike extends RectangleLike {
  constructor(x = 0, y = 0, width = 0, height = 0) {
    super(x, y, width, height);
    this.relative = false;
  }
}

class GraphLike {
  insertVertex(...args) {
    let parent;
    let id;
    let value;
    let x;
    let y;
    let width;
    let height;
    let style;
    let relative;
    let geometryClass;
    [parent, id, value, x, y, width, height, style, relative, geometryClass] = args;
    const Geometry = geometryClass || GeometryLike;
    const geometry = new Geometry(x, y, width, height);
    return [parent, id, value, geometry.x, geometry.y, geometry.width, geometry.height,
      style === undefined, relative === undefined].join(":");
  }
}

function plain(...args) {
  let a;
  let b;
  let c;
  let d;
  let e;
  let f;
  let g;
  [a, b, c, d, e, f, g] = args;
  return [a, b, c, d, e, f, g].join(":");
}

const VertexLike = {
  insertVertex(...args) {
    let parent;
    let id;
    let value;
    let x;
    let y;
    let width;
    let height;
    let style;
    let relative;
    let geometryClass;
    [parent, id, value, x, y, width, height, style, relative, geometryClass] = args;
    return this.createVertex(parent, id, value, x, y, width, height, style, relative, geometryClass);
  }
};

const VertexMixinLike = {
  insertVertex: VertexLike.insertVertex,
  createVertex(_parent, id, value, x, y, width, height, style, relative = false, geometryClass = GeometryLike) {
    const geometry = new geometryClass(x, y, width, height);
    geometry.relative = relative;
    return [id, value, geometry.x, geometry.y, geometry.width, geometry.height].join(":");
  }
};

const mixinGraph = {
  createVertex(_parent, id, value, x, y, width, height, style, relative = false, geometryClass = GeometryLike) {
    const geometry = new geometryClass(x, y, width, height);
    return [id, value, geometry.x, geometry.y, geometry.width, geometry.height].join(":");
  }
};
mixinGraph.insertVertex = VertexLike.insertVertex;
mixinGraph.batchUpdate = (callback) => callback();

class MixedGraph {}
for (const key of Reflect.ownKeys(VertexMixinLike)) {
  Object.defineProperty(MixedGraph.prototype, key, { value: VertexMixinLike[key], writable: true });
}

const graph = new GraphLike();
console.log(graph.insertVertex("parent", "source", "Source", 40, 45, 110, 50));
console.log(plain("a", "b", "c", "d", "e", "f", "g"));
console.log(mixinGraph.insertVertex("parent", "source", "Source", 40, 45, 110, 50));
let batchedVertex = "missing";
mixinGraph.batchUpdate(() => {
  batchedVertex = mixinGraph.insertVertex("parent", "source", "Source", 40, 45, 110, 50);
});
console.log(batchedVertex);
console.log(new MixedGraph().insertVertex("parent", "source", "Source", 40, 45, 110, 50));
