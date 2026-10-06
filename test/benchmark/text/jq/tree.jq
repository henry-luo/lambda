# jq_tree: path machinery under load. jaq's examples/benches tree-update,
# tree-paths and tree-flatten (MIT) over a complete binary tree.
def rounds: 7;
def depth: 17;
def tree($d): nth($d; 0 | recurse([., .]));
tree(depth) as $t
| reduce range(rounds) as $r (0;
    ($t | (.. | scalars) |= . + 1 + $r) as $u
    | (. * 31 + ($u | [paths] | length) + ($u | flatten | add)) % 1000000007)
