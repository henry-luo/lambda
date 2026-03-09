// AWFY Benchmark: DeltaBlue (standalone bundle)
// Auto-generated from official AWFY source — no require() needed

// --- benchmark.js ---
// @ts-check
// This code is derived from the SOM benchmarks, see AUTHORS.md file.
//
// Copyright (c) 2015-2016 Stefan Marr <git@stefan-marr.de>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the 'Software'), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED 'AS IS', WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

class Benchmark {
  innerBenchmarkLoop(innerIterations) {
    for (let i = 0; i < innerIterations; i += 1) {
      if (!this.verifyResult(this.benchmark())) {
        return false;
      }
    }
    return true;
  }

  benchmark() {
    throw new Error('subclass responsibility');
  }

  verifyResult() {
    throw new Error('subclass responsibility');
  }
}



// --- som.js ---
// @ts-check
// This code is derived from the SOM benchmarks, see AUTHORS.md file.
//
// Copyright (c) 2015-2016 Stefan Marr <git@stefan-marr.de>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the 'Software'), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED 'AS IS', WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

const INITIAL_SIZE = 10;
const INITIAL_CAPACITY = 16;

class Vector {
  constructor(size) {
    this.storage = size === undefined || size === 0 ? null : new Array(size);
    this.firstIdx = 0;
    this.lastIdx = 0;
  }

  static with(elem) {
    const v = new Vector(1);
    v.append(elem);
    return v;
  }

  at(idx) {
    if (this.storage === null || idx >= this.storage.length) {
      return null;
    }
    return this.storage[idx];
  }

  atPut(idx, val) {
    if (this.storage === null) {
      this.storage = new Array(Math.max(idx + 1, INITIAL_SIZE));
    } else if (idx >= this.storage.length) {
      let newLength = this.storage.length;
      while (newLength <= idx) {
        newLength *= 2;
      }
      this.storage = this.storage.slice();
      this.storage.length = newLength;
    }
    this.storage[idx] = val;
    if (this.lastIdx < idx + 1) {
      this.lastIdx = idx + 1;
    }
  }

  append(elem) {
    if (this.storage === null) {
      this.storage = new Array(INITIAL_SIZE);
    } else if (this.lastIdx >= this.storage.length) {
      // Copy storage to comply with rules, but don't extend storage
      const newLength = this.storage.length * 2;
      this.storage = this.storage.slice();
      this.storage.length = newLength;
    }

    this.storage[this.lastIdx] = elem;
    this.lastIdx += 1;
  }

  isEmpty() {
    return this.lastIdx === this.firstIdx;
  }

  forEach(fn) {
    for (let i = this.firstIdx; i < this.lastIdx; i += 1) {
      fn(this.storage[i]);
    }
  }

  hasSome(fn) {
    for (let i = this.firstIdx; i < this.lastIdx; i += 1) {
      if (fn(this.storage[i])) {
        return true;
      }
    }
    return false;
  }

  getOne(fn) {
    for (let i = this.firstIdx; i < this.lastIdx; i += 1) {
      const e = this.storage[i];
      if (fn(e)) {
        return e;
      }
    }
    return null;
  }

  removeFirst() {
    if (this.isEmpty()) {
      return null;
    }
    this.firstIdx += 1;
    return this.storage[this.firstIdx - 1];
  }

  remove(obj) {
    if (this.storage === null || this.isEmpty()) {
      return false;
    }

    const newArray = new Array(this.capacity());
    let newLast = 0;
    let found = false;

    this.forEach((it) => {
      if (it === obj) {
        found = true;
      } else {
        newArray[newLast] = it;
        newLast += 1;
      }
    });

    this.storage = newArray;
    this.lastIdx = newLast;
    this.firstIdx = 0;
    return found;
  }

  removeAll() {
    this.firstIdx = 0;
    this.lastIdx = 0;
    if (this.storage !== null) {
      this.storage = new Array(this.storage.length);
    }
  }

  size() {
    return this.lastIdx - this.firstIdx;
  }

  capacity() {
    return this.storage === null ? 0 : this.storage.length;
  }

  // eslint-disable-next-line no-unused-vars
  swap(storage, i, j) {
    throw new Error('Not Implemented');
  }

  // eslint-disable-next-line no-unused-vars
  defaultSort(i, j) {
    throw new Error('Not Implemented');
  }

  sortRange(i, j, compare) {
    if (!compare) {
      this.defaultSort(i, j);
    }

    const n = j + 1 - i;
    if (n <= 1) {
      return;
    }

    let di = this.storage[i];
    let dj = this.storage[j];

    if (compare(di, dj)) {
      this.swap(this.storage, i, j);
      const tt = di;
      di = dj;
      dj = tt;
    }

    if (n > 2) {
      const ij = (i + j) / 2;
      let dij = this.storage[ij];

      if (compare(di, dij) <= 0) {
        if (!compare(dij, dj)) {
          this.swap(this.storage, j, ij);
          dij = dj;
        }
      } else {
        this.swap(this.storage, i, ij);
        dij = di;
      }

      if (n > 3) {
        let k = i;
        let l = j - 1;

        // eslint-disable-next-line no-constant-condition
        while (true) {
          while (k <= l && compare(dij, this.storage[l])) {
            l -= 1;
          }

          k += 1;
          while (k <= l && compare(this.storage[k], dij)) {
            k += 1;
          }

          if (k > l) {
            break;
          }
          this.swap(this.storage, k, l);
        }

        const c = null; // never used
        this.sort(i, l, c);
        this.sort(k, j, c);
      }
    }
  }

  // eslint-disable-next-line no-unused-vars
  sort(compare, i, c) {
    if (this.size() > 0) {
      this.sortRange(this.firstIdx, this.lastIdx - 1, compare);
    }
  }
}

class Set {
  constructor(size) {
    this.items = new Vector(size === undefined ? INITIAL_SIZE : size);
  }

  size() {
    return this.items.size();
  }

  forEach(fn) {
    this.items.forEach(fn);
  }

  hasSome(fn) {
    return this.items.hasSome(fn);
  }

  getOne(fn) {
    return this.items.getOne(fn);
  }

  add(obj) {
    if (!this.contains(obj)) {
      this.items.append(obj);
    }
  }

  contains(obj) {
    return this.hasSome((e) => e === obj);
  }

  removeAll() {
    this.items.removeAll();
  }

  collect(fn) {
    const coll = new Vector();
    this.forEach((e) => coll.append(fn(e)));
    return coll;
  }
}

class IdentitySet extends Set {
  constructor(size) {
    super(size === undefined ? INITIAL_SIZE : size);
  }

  contains(obj) {
    return this.hasSome((e) => e === obj);
  }
}

class DictEntry {
  constructor(hash, key, value, next) {
    this.hash = hash;
    this.key = key;
    this.value = value;
    this.next = next;
  }

  match(hash, key) {
    return this.hash === hash && key === this.key;
  }
}

function hashFn(key) {
  if (!key) {
    return 0;
  }
  const hash = key.customHash();
  return hash ^ (hash >>> 16);
}

class Dictionary {
  constructor(size) {
    this.buckets = new Array(size === undefined ? INITIAL_CAPACITY : size);
    this.size_ = 0;
  }

  size() {
    return this.size_;
  }

  isEmpty() {
    return this.size_ === 0;
  }

  getBucketIdx(hash) {
    return (this.buckets.length - 1) & hash;
  }

  getBucket(hash) {
    return this.buckets[this.getBucketIdx(hash)];
  }

  at(key) {
    const hash_ = hashFn(key);
    let e = this.getBucket(hash_);

    while (e) {
      if (e.match(hash_, key)) {
        return e.value;
      }
      e = e.next;
    }
    return null;
  }

  containsKey(key) {
    const hash_ = hashFn(key);
    let e = this.getBucket(hash_);

    while (e) {
      if (e.match(hash_, key)) {
        return true;
      }
      e = e.next;
    }
    return false;
  }

  atPut(key, value) {
    const hash_ = hashFn(key);
    const i = this.getBucketIdx(hash_);
    const current = this.buckets[i];

    if (!current) {
      this.buckets[i] = this.newEntry(key, value, hash_);
      this.size_ += 1;
    } else {
      this.insertBucketEntry(key, value, hash_, current);
    }

    if (this.size_ > this.buckets.length) {
      this.resize();
    }
  }

  newEntry(key, value, hash) {
    return new DictEntry(hash, key, value, null);
  }

  insertBucketEntry(key, value, hash, head) {
    let current = head;

    // eslint-disable-next-line no-constant-condition
    while (true) {
      if (current.match(hash, key)) {
        current.value = value;
        return;
      }
      if (!current.next) {
        this.size_ += 1;
        current.next = this.newEntry(key, value, hash);
        return;
      }
      current = current.next;
    }
  }

  resize() {
    const oldStorage = this.buckets;
    this.buckets = new Array(oldStorage.length * 2);
    this.transferEntries(oldStorage);
  }

  transferEntries(oldStorage) {
    for (let i = 0; i < oldStorage.length; i += 1) {
      const current = oldStorage[i];
      if (current) {
        oldStorage[i] = null;

        if (!current.next) {
          this.buckets[current.hash & (this.buckets.length - 1)] = current;
        } else {
          this.splitBucket(oldStorage, i, current);
        }
      }
    }
  }

  splitBucket(oldStorage, i, head) {
    let loHead = null;
    let loTail = null;
    let hiHead = null;
    let hiTail = null;
    let current = head;

    while (current) {
      if ((current.hash & oldStorage.length) === 0) {
        if (!loTail) {
          loHead = current;
        } else {
          loTail.next = current;
        }
        loTail = current;
      } else {
        if (!hiTail) {
          hiHead = current;
        } else {
          hiTail.next = current;
        }
        hiTail = current;
      }
      current = current.next;
    }

    if (loTail) {
      loTail.next = null;
      this.buckets[i] = loHead;
    }
    if (hiTail) {
      hiTail.next = null;
      this.buckets[i + oldStorage.length] = hiHead;
    }
  }

  removeAll() {
    this.buckets = new Array(this.buckets.length);
    this.size_ = 0;
  }

  getKeys() {
    const keys = new Vector(this.size_);
    for (let i = 0; i < this.buckets.length; i += 1) {
      let current = this.buckets[i];
      while (current) {
        keys.append(current.key);
        current = current.next;
      }
    }
    return keys;
  }

  getValues() {
    const values = new Vector(this.size_);
    for (let i = 0; i < this.buckets.length; i += 1) {
      let current = this.buckets[i];
      while (current) {
        values.append(current.value);
        current = current.next;
      }
    }
    return values;
  }
}

class DictIdEntry extends DictEntry {
  match(hash, key) {
    return this.hash === hash && this.key === key;
  }
}

class IdentityDictionary extends Dictionary {
  constructor(size) {
    super(size === undefined ? INITIAL_CAPACITY : size);
  }

  newEntry(key, value, hash) {
    return new DictIdEntry(hash, key, value, null);
  }
}

class Random {
  constructor() {
    this.seed = 74755;
  }

  next() {
    this.seed = (this.seed * 1309 + 13849) & 65535;
    return this.seed;
  }
}








const som = { Vector, Random, Set, IdentitySet, Dictionary, IdentityDictionary };

// --- deltablue.js ---
// @ts-check
// The benchmark in its current state is a derivation from the SOM version,
// which is derived from Mario Wolczko's Smalltalk version of DeltaBlue.
//
// The original license details are available here:
// http://web.archive.org/web/20050825101121/http://www.sunlabs.com/people/mario/java_benchmarking/index.html



class Plan extends Vector {
  constructor() {
    super(15);
  }

  execute() {
    this.forEach((c) => c.execute());
  }
}

class Sym {
  constructor(hash) {
    this.hash = hash;
  }

  customHash() {
    return this.hash;
  }
}

const ABSOLUTE_STRONGEST = new Sym(0);
const REQUIRED = new Sym(1);
const STRONG_PREFERRED = new Sym(2);
const PREFERRED = new Sym(3);
const STRONG_DEFAULT = new Sym(4);
const DEFAULT = new Sym(5);
const WEAK_DEFAULT = new Sym(6);
const ABSOLUTE_WEAKEST = new Sym(7);

function createStrengthTable() {
  const strengthTable = new IdentityDictionary();
  strengthTable.atPut(ABSOLUTE_STRONGEST, -10000);
  strengthTable.atPut(REQUIRED, -800);
  strengthTable.atPut(STRONG_PREFERRED, -600);
  strengthTable.atPut(PREFERRED, -400);
  strengthTable.atPut(STRONG_DEFAULT, -200);
  strengthTable.atPut(DEFAULT, 0);
  strengthTable.atPut(WEAK_DEFAULT, 500);
  strengthTable.atPut(ABSOLUTE_WEAKEST, 10000);
  return strengthTable;
}

class Strength {
  constructor(symbolicValue) {
    this.arithmeticValue = Strength.strengthTable.at(symbolicValue);
  }

  sameAs(s) {
    return this.arithmeticValue === s.arithmeticValue;
  }

  stronger(s) {
    return this.arithmeticValue < s.arithmeticValue;
  }

  weaker(s) {
    return this.arithmeticValue > s.arithmeticValue;
  }

  strongest(s) {
    return s.stronger(this) ? s : this;
  }

  weakest(s) {
    return s.weaker(this) ? s : this;
  }

  static of(strength) {
    return Strength.strengthConstant.at(strength);
  }

  static strengthTable = createStrengthTable();

  static createStrengthConstants() {
    const strengthConstant = new IdentityDictionary();
    Strength.strengthTable.getKeys().forEach((key) => {
      strengthConstant.atPut(key, new Strength(key));
    });
    return strengthConstant;
  }

  static strengthConstant = Strength.createStrengthConstants();

  static absoluteWeakest = Strength.of(ABSOLUTE_WEAKEST);

  static required = Strength.of(REQUIRED);
}

class AbstractConstraint {
  constructor(strengthSym) {
    this.strength = Strength.of(strengthSym);
  }

  isInput() {
    return false;
  }

  addConstraint(planner) {
    this.addToGraph();
    planner.incrementalAdd(this);
  }

  destroyConstraint(planner) {
    if (this.isSatisfied()) {
      planner.incrementalRemove(this);
    }
    this.removeFromGraph();
  }

  inputsKnown(mark) {
    return !this.inputsHasOne((v) => !(v.mark === mark || v.stay || v.determinedBy === null));
  }

  satisfy(mark, planner) {
    let overridden;
    this.chooseMethod(mark);

    if (this.isSatisfied()) {
      // constraint can be satisfied
      // mark inputs to allow cycle detection in addPropagate
      this.inputsDo((i) => { i.mark = mark; });

      const out = this.getOutput();
      overridden = out.determinedBy;
      if (overridden !== null) {
        overridden.markUnsatisfied();
      }
      out.determinedBy = this;
      if (!planner.addPropagate(this, mark)) {
        throw new Error('Cycle encountered');
      }
      out.mark = mark;
    } else {
      overridden = null;
      if (this.strength.sameAs(Strength.required)) {
        throw new Error('Could not satisfy a required constraint');
      }
    }
    return overridden;
  }
}

class BinaryConstraint extends AbstractConstraint {
  // eslint-disable-next-line no-unused-vars
  constructor(var1, var2, strength, planner) {
    super(strength);
    this.v1 = var1;
    this.v2 = var2;
    this.direction = null;
  }

  isSatisfied() {
    return this.direction !== null;
  }

  addToGraph() {
    this.v1.addConstraint(this);
    this.v2.addConstraint(this);
    this.direction = null;
  }

  removeFromGraph() {
    if (this.v1 !== null) {
      this.v1.removeConstraint(this);
    }
    if (this.v2 !== null) {
      this.v2.removeConstraint(this);
    }
    this.direction = null;
  }

  chooseMethod(mark) {
    if (this.v1.mark === mark) {
      if (this.v2.mark !== mark && this.strength.stronger(this.v2.walkStrength)) {
        this.direction = 'forward';
        return this.direction;
      }
      this.direction = null;
      return this.direction;
    }

    if (this.v2.mark === mark) {
      if (this.v1.mark !== mark && this.strength.stronger(this.v1.walkStrength)) {
        this.direction = 'backward';
        return this.direction;
      }
      this.direction = null;
      return this.direction;
    }

    // If we get here, neither variable is marked, so we have a choice.
    if (this.v1.walkStrength.weaker(this.v2.walkStrength)) {
      if (this.strength.stronger(this.v1.walkStrength)) {
        this.direction = 'backward';
        return this.direction;
      }
      this.direction = null;
      return this.direction;
    }
    if (this.strength.stronger(this.v2.walkStrength)) {
      this.direction = 'forward';
      return this.direction;
    }
    this.direction = null;
    return this.direction;
  }

  inputsDo(fn) {
    if (this.direction === 'forward') {
      fn(this.v1);
    } else {
      fn(this.v2);
    }
  }

  inputsHasOne(fn) {
    if (this.direction === 'forward') {
      return fn(this.v1);
    }
    return fn(this.v2);
  }

  markUnsatisfied() {
    this.direction = null;
  }

  getOutput() {
    return this.direction === 'forward' ? this.v2 : this.v1;
  }

  recalculate() {
    let ihn;
    let out;

    if (this.direction === 'forward') {
      ihn = this.v1; out = this.v2;
    } else {
      ihn = this.v2; out = this.v1;
    }

    out.walkStrength = this.strength.weakest(ihn.walkStrength);
    out.stay = ihn.stay;
    if (out.stay) {
      this.execute();
    }
  }
}

class UnaryConstraint extends AbstractConstraint {
  constructor(v, strength, planner) {
    super(strength);
    this.output = v;
    this.satisfied = false;

    this.addConstraint(planner);
  }

  isSatisfied() {
    return this.satisfied;
  }

  addToGraph() {
    this.output.addConstraint(this);
    this.satisfied = false;
  }

  removeFromGraph() {
    if (this.output !== null) {
      this.output.removeConstraint(this);
    }
    this.satisfied = false;
  }

  chooseMethod(mark) {
    this.satisfied = this.output.mark !== mark
      && this.strength.stronger(this.output.walkStrength);
    return null;
  }

  // eslint-disable-next-line no-unused-vars
  inputsDo(fn) {
    // I have no input variables
  }

  // eslint-disable-next-line no-unused-vars
  inputsHasOne(fn) {
    return false;
  }

  markUnsatisfied() {
    this.satisfied = false;
  }

  getOutput() {
    return this.output;
  }

  recalculate() {
    this.output.walkStrength = this.strength;
    this.output.stay = !this.isInput();
    if (this.output.stay) {
      this.execute(); // stay optimization
    }
  }
}

class EditConstraint extends UnaryConstraint {
  isInput() {
    return true;
  }

  execute() {}
}

class EqualityConstraint extends BinaryConstraint {
  constructor(var1, var2, strength, planner) {
    super(var1, var2, strength, planner);
    this.addConstraint(planner);
  }

  execute() {
    if (this.direction === 'forward') {
      this.v2.value = this.v1.value;
    } else {
      this.v1.value = this.v2.value;
    }
  }
}

class ScaleConstraint extends BinaryConstraint {
  constructor(src, scale, offset, dest, strength, planner) {
    super(src, dest, strength, planner);
    this.scale = scale;
    this.offset = offset;

    this.addConstraint(planner);
  }

  addToGraph() {
    this.v1.addConstraint(this);
    this.v2.addConstraint(this);
    this.scale.addConstraint(this);
    this.offset.addConstraint(this);
    this.direction = null;
  }

  removeFromGraph() {
    if (this.v1 !== null) { this.v1.removeConstraint(this); }
    if (this.v2 !== null) { this.v2.removeConstraint(this); }
    if (this.scale !== null) { this.scale.removeConstraint(this); }
    if (this.offset !== null) { this.offset.removeConstraint(this); }
    this.direction = null;
  }

  execute() {
    if (this.direction === 'forward') {
      this.v2.value = this.v1.value * this.scale.value + this.offset.value;
    } else {
      this.v1.value = (this.v2.value - this.offset.value) / this.scale.value;
    }
  }

  inputsDo(fn) {
    if (this.direction === 'forward') {
      fn(this.v1);
      fn(this.scale);
      fn(this.offset);
    } else {
      fn(this.v2);
      fn(this.scale);
      fn(this.offset);
    }
  }

  recalculate() {
    let ihn;
    let out;

    if (this.direction === 'forward') {
      ihn = this.v1; out = this.v2;
    } else {
      out = this.v1; ihn = this.v2;
    }

    out.walkStrength = this.strength.weakest(ihn.walkStrength);
    out.stay = ihn.stay && this.scale.stay && this.offset.stay;
    if (out.stay) {
      this.execute(); // stay optimization
    }
  }
}

class StayConstraint extends UnaryConstraint {
  execute() {}
}

class Variable {
  constructor() {
    this.value = 0;
    this.constraints = new Vector(2);
    this.determinedBy = null;
    this.walkStrength = Strength.absoluteWeakest;
    this.stay = true;
    this.mark = 0;
  }

  addConstraint(c) {
    this.constraints.append(c);
  }

  removeConstraint(c) {
    this.constraints.remove(c);
    if (this.determinedBy === c) {
      this.determinedBy = null;
    }
  }

  static value(aValue) {
    const v = new Variable();
    v.value = aValue;
    return v;
  }
}

class Planner {
  constructor() {
    this.currentMark = 1;
  }

  newMark() {
    this.currentMark += 1;
    return this.currentMark;
  }

  incrementalAdd(c) {
    const mark = this.newMark();
    let overridden = c.satisfy(mark, this);

    while (overridden !== null) {
      overridden = overridden.satisfy(mark, this);
    }
  }

  incrementalRemove(c) {
    const out = c.getOutput();
    c.markUnsatisfied();
    c.removeFromGraph();

    const unsatisfied = this.removePropagateFrom(out);
    unsatisfied.forEach((u) => this.incrementalAdd(u));
  }

  extractPlanFromConstraints(constraints) {
    const sources = new Vector();

    constraints.forEach((c) => {
      if (c.isInput() && c.isSatisfied()) {
        sources.append(c);
      }
    });

    return this.makePlan(sources);
  }

  makePlan(sources) {
    const mark = this.newMark();
    const plan = new Plan();
    const todo = sources;

    while (!todo.isEmpty()) {
      const c = todo.removeFirst();

      if (c.getOutput().mark !== mark && c.inputsKnown(mark)) {
        // not in plan already and eligible for inclusion
        plan.append(c);
        c.getOutput().mark = mark;
        this.addConstraintsConsumingTo(c.getOutput(), todo);
      }
    }
    return plan;
  }

  propagateFrom(v) {
    const todo = new Vector();
    this.addConstraintsConsumingTo(v, todo);

    while (!todo.isEmpty()) {
      const c = todo.removeFirst();
      c.execute();
      this.addConstraintsConsumingTo(c.getOutput(), todo);
    }
  }

  addConstraintsConsumingTo(v, coll) {
    const determiningC = v.determinedBy;

    v.constraints.forEach((c) => {
      if (c !== determiningC && c.isSatisfied()) {
        coll.append(c);
      }
    });
  }

  addPropagate(c, mark) {
    const todo = Vector.with(c);

    while (!todo.isEmpty()) {
      const d = todo.removeFirst();

      if (d.getOutput().mark === mark) {
        this.incrementalRemove(c);
        return false;
      }
      d.recalculate();
      this.addConstraintsConsumingTo(d.getOutput(), todo);
    }
    return true;
  }

  change(v, newValue) {
    const editC = new EditConstraint(v, PREFERRED, this);
    const editV = Vector.with(editC);
    const plan = this.extractPlanFromConstraints(editV);

    for (let i = 0; i < 10; i += 1) {
      v.value = newValue;
      plan.execute();
    }
    editC.destroyConstraint(this);
  }

  constraintsConsuming(v, fn) {
    const determiningC = v.determinedBy;
    v.constraints.forEach((c) => {
      if (c !== determiningC && c.isSatisfied()) {
        fn(c);
      }
    });
  }

  removePropagateFrom(out) {
    const unsatisfied = new Vector();

    out.determinedBy = null;
    out.walkStrength = Strength.absoluteWeakest;
    out.stay = true;

    const todo = Vector.with(out);

    while (!todo.isEmpty()) {
      const v = todo.removeFirst();

      v.constraints.forEach((c) => {
        if (!c.isSatisfied()) { unsatisfied.append(c); }
      });

      this.constraintsConsuming(v, (c) => {
        c.recalculate();
        todo.append(c.getOutput());
      });
    }

    unsatisfied.sort((c1, c2) => c1.strength.stronger(c2.strength));
    return unsatisfied;
  }

  static chainTest(n) {
    const planner = new Planner();
    const vars = new Array(n + 1);

    for (let i = 0; i < n + 1; i += 1) {
      vars[i] = new Variable();
    }

    // Build chain of n equality constraints
    for (let i = 0; i < n; i += 1) {
      const v1 = vars[i];
      const v2 = vars[i + 1];
      new EqualityConstraint(v1, v2, REQUIRED, planner);
    }

    new StayConstraint(vars[n], STRONG_DEFAULT, planner);

    const editC = new EditConstraint(vars[0], PREFERRED, planner);
    const editV = Vector.with(editC);
    const plan = planner.extractPlanFromConstraints(editV);

    for (let i = 0; i < 100; i += 1) {
      vars[0].value = i;
      plan.execute();
      if (vars[n].value !== i) {
        throw new Error('Chain test failed!');
      }
    }
    editC.destroyConstraint(planner);
  }

  static projectionTest(n) {
    const planner = new Planner();
    const dests = new Vector();
    const scale = Variable.value(10);
    const offset = Variable.value(1000);

    let src = null;
    let dst = null;

    for (let i = 1; i <= n; i += 1) {
      src = Variable.value(i);
      dst = Variable.value(i);
      dests.append(dst);
      new StayConstraint(src, DEFAULT, planner);
      new ScaleConstraint(src, scale, offset, dst, REQUIRED, planner);
    }

    planner.change(src, 17);
    if (dst.value !== 1170) {
      throw new Error('Projection test 1 failed!');
    }

    planner.change(dst, 1050);
    if (src.value !== 5) {
      throw new Error('Projection test 2 failed!');
    }

    planner.change(scale, 5);
    for (let i = 0; i < n - 1; i += 1) {
      if (dests.at(i).value !== (i + 1) * 5 + 1000) {
        throw new Error('Projection test 3 failed!');
      }
    }

    planner.change(offset, 2000);
    for (let i = 0; i < n - 1; i += 1) {
      if (dests.at(i).value !== (i + 1) * 5 + 2000) {
        throw new Error('Projection test 4 failed!');
      }
    }
  }
}

class DeltaBlue extends Benchmark {
  innerBenchmarkLoop(innerIterations) {
    Planner.chainTest(innerIterations);
    Planner.projectionTest(innerIterations);
    return true;
  }
}



// --- timing harness ---
const bench = new DeltaBlue();
const __t0 = process.hrtime.bigint();
// Synchronized with JetStream: 20 iterations of chainTest(100) + projectionTest(100)
let ok = true;
for (let i = 0; i < 20; ++i) {
    ok = bench.innerBenchmarkLoop(100) && ok;
}
const __t1 = process.hrtime.bigint();
process.stdout.write("DeltaBlue: " + (ok ? "PASS" : "FAIL") + "\n");
process.stdout.write("__TIMING__:" + Number(__t1 - __t0) / 1e6 + "\n");
