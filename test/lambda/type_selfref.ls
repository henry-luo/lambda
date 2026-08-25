// self-referential type declarations must resolve their own name while the
// body parses (regression: the name degraded to ANY, so any value matched the
// recursive field). The `false` rows below prove the field is NOT ANY.
type N = {k: int, next: N?}
({k: 1, next: null} is N);
({k: 1, next: {k: 2, next: null}} is N);
({k: 1, next: 5} is N);
let t: N = {k: 10, next: {k: 20, next: null}}
t.k
t.next.k
type Node = {left: Node?, right: Node?, val: int}
({val: 3, left: {val: 1, left: null, right: null}, right: null} is Node);
({val: 3, left: 7, right: null} is Node)
