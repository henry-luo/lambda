// S4.2.2/D3.3.3v3: a binder joins its parameter sites and specializes T.
fn pick(T: type, a: T) T => a;
fn selected(T: type) type => T;
fn merge(a: number as U, b: number as U, c: U) U => c;
fn map_value(a: {x: number as V}, b: V) V => b;
fn array_value(a: (int as E)[], b: E) E => b;
fn constrained(a: number that true as W, b: W) W => b;

let x: int = pick(int, 1);
let left: any = 1;
let right: any = 2.0;
let tail: any = 3;
let record: any = {x: 1};
let values: any = [1];
[selected(int), x, merge(left, right, tail), map_value(record, 2), array_value(values, 2), constrained(1, 2)]
