// S4.2.2/D8.3.1v2: four exact binder variants, then boxed fallback.
fn identity(T: type, value: T) T => value;
// This definition deliberately precedes its binder callee: its direct edge
// must use the raw forward declaration, not wait for body emission order.
fn increment_caller(value: int) int => increment(value);
fn increment(value: number as N) N => value + 1;
// Exact non-scalar keys use the established raw container-pointer lane.
fn map_size(value: map as M) int => len(value);
fn array_size(value: array as A) int => len(value);
// A dynamic local edge is eligible for the optional caller guard hoist.
fn selected_dynamic(value: any as T) type => T;
fn selected_dynamic_caller(value: any) type => selected_dynamic(value) or any;
// Shape-bearing containers require descriptor equality, not a map/element tag.
type Point { x: int, y: int }
type Note { label: string, string* }
fn point_size(value: Point as P) int => len(value);
fn note_size(value: Note as N) int => len(value);
// A base-typed edge may carry a derived descriptor at runtime. Its wrapper
// guard must miss and let the boxed binder observe the derived identity.
type Shape { x: int }
type Circle : Shape { radius: int }
fn shape_name(value: Shape as S) string => string(name(S));
fn through_shape(value: Shape) string => shape_name(value);

[identity(int, 1), identity(float, 2.5), identity(bool, true),
 identity(string, "four"), identity(i64, 5i64), identity(int, 6),
 increment(10), increment(1.5), increment_caller(12), map_size({a: 7}),
 array_size([8, "nine"]), selected_dynamic(9), selected_dynamic_caller(10),
 point_size(<Point x: 3, y: 4>),
 note_size(<Note label: "n", "body">), shape_name(<Shape x: 5>),
 through_shape(<Circle x: 6, radius: 7>)]
