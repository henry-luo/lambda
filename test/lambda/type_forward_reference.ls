// S11.4.12: a top-level type name binds for the whole module, so a type may
// name a type declared later, or one that names it back. Each reference here
// read as `any` before the ruling, so every `false` below was `true`.

// a schema written root first
type Library = {name: string, books: Book[]}
type Book = {title: string, year: Year}
type Year = int

// mutually recursive maps, elements and a union through a field
type A = {b: B?}
type B = {a: A?}
type P = <p Q*>
type Q = <q P*>
type Part = string | Msg
type Msg = {subject: string, parts: Part[]}

// an alias of a later alias, and of a later union
type Title = Name
type Name = {first: string}
type Id = Key
type Key = int | string

// annotations, object fields and match arms before their types
fn year_of(b: Book) Year => b.year
type Shelf { label: Label }
type Label = string
fn kind(x) => match x {
  case Count: "count"
  default: "other"
}
type Count = int

// a pattern naming a later pattern
type Code = \(Digit+)
type Digit = \("0" to "9")

let lib = {name: "city", books: [{title: "t", year: 1999}]}
let lib_bad = {name: "city", books: [{title: "t", year: "1999"}]}
let ab = {b: {a: {b: null}}}
let ab_bad = {b: {a: 5}}
let pq = <p <q <p>>>
let pq_bad = <p <q 5>>
let msg = {subject: "s", parts: ["a", {subject: "t", parts: []}]}
let msg_bad = {subject: "s", parts: [5]};

"1 a schema written root first";
[lib is Library, lib_bad is Library];

"2 mutual recursion through fields, content and a union";
[ab is A, ab_bad is A, pq is P, pq_bad is P, msg is Msg, msg_bad is Msg];

"3 an alias of a later alias or union";
[{first: "x"} is Title, {first: 5} is Title, 5 is Id, "k" is Id, true is Id];

"4 annotations, object fields and match arms";
[year_of({title: "t", year: 2001}), <Shelf label: "a"> is Shelf, kind(3), kind("s")];

"5 a pattern naming a later pattern";
["123" is Code, "12a" is Code, "" is Code]
