// S11.1.6v3: an element pattern's content section is a sequence-pattern slot.
// The children match as `[c, d]` matches an array -- runs, literal items, and
// the whole content. D2.6.6v3: the pattern lives on the declared type only.

type UL = <ul; <li>*>
type NonEmpty = <ul; <li>+>
type Opt = <ul; <li>?>
type Pair = <p; string, int>
type Open = <div; <h1>, any*>
type Bare = <div>
type Lit = <b; "bold">
type Note { label: string, string* }
type Memo : Note { extra: int }

let three = <ul <li "a"> <li "b"> <li "c">>;
let empty_ul = <ul>;
let one = <ul <li "a">>;
let ps = <ul <p "a"> <p "b">>;
let l = <li "a">;
let m = <Memo label: "a", extra: 1, "x">;

// a: a run consumes any number of matching children, and nothing else
'=runs=';
[three is UL, empty_ul is UL, ps is UL];
[three is NonEmpty, empty_ul is NonEmpty];
[empty_ul is Opt, one is Opt, three is Opt];

// b: run-free slots are positional and exact
'=positional=';
[<p "x" 3> is Pair, <p 3 "x"> is Pair, <p "x"> is Pair];

// c: open content is a trailing any*; a pattern without a content section
// leaves content unconstrained
'=open=';
[<div <h1 "t"> <p "a"> 3> is Open, <div <h1 "t">> is Open, <div <p "a">> is Open];
[<div> is Bare, <div 1 2 3> is Bare];

// d: a literal item matches by value
'=literal=';
[<b "bold"> is Lit, <b "thin"> is Lit];

// e: content is normalized before it is matched (S2.6.4): adjacent strings
// merge into one item
'=normalized=';
[<p "a" "b"> is <p; string, string>, <p "a" "b"> is <p; string>];

// f: a structural slot validates in full; only a bare kind reduces to a TypeId
'=slots=';
[[l] is [<p>], [l] is [<li>], [{x: 1}] is [{y: int}], [[1, 2]] is [[int, int, int]], [l, 2] is [element, int]];

// g: a derived nominal type keeps its base's kind and content pattern
// (S2.1.3v2, OB7)
'=nominal=';
[m is element, m is Note, len(m)]
