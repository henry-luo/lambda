// A `type` alias is bound before its body resolves, so an element type may
// name itself. Its self-references held the pre-bound placeholder, which was
// published for maps and unions but stayed an empty map for elements: a nested
// child was matched against `{}`, and `<s <s>> is S` answered false.

type S = <s S*>
type T = <t a: int, T*>
type M = <m kids: M[]?>
type Tree = <tree label: string, (Tree | <leaf>)*>
type U = <u> | <v U*>

let s3 = <s <s <s>>>;
let t3 = <t a: 1, <t a: 2, <t a: 3>>>;
let t_bad = <t a: 1, <t a: "x">>;
let m3 = <m kids: [<m kids: [<m>]>]>;
let tree = <tree label: "r", <leaf> <tree label: "c", <leaf>>>;
let tree_bad = <tree label: "r", <tree label: 5>>;
let u3 = <v <v <u>>>;

"1 content naming its own type";
[<s> is S, <s <s>> is S, s3 is S, <s 5> is S, <s <x>> is S];

"2 attributes checked at every level";
[t3 is T, t_bad is T];

"3 an attribute naming its own type";
[<m> is M, <m kids: [<m>]> is M, m3 is M, <m kids: [<x>]> is M];

"4 recursion through a union in content";
[tree is Tree, tree_bad is Tree, u3 is U, <v 5> is U];

"5 a parameter contract and a match arm";
fn depth(x: S) int => if (len(x) == 0) 1 else 1 + depth(x[0]);
fn kind(x) => match x {
  case S: "s-tree"
  default: "other"
};
[depth(s3), kind(s3), kind(t3)]
