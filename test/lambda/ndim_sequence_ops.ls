// S1.6: an N-D numeric array is a sequence of its leading-axis rows, however it
// is stored. `[[1,2],[3,4],[5,6]]` is promoted to one 2-D ArrayNum; reverse,
// sort, unique, take, drop, and slice must act on its rows exactly as indexing
// and `for … in` do, never on the flat storage.
let nd = [[1, 2], [3, 4], [5, 6]]
let rows = [[5, 6], [1, 2], [3, 4]];
[len(nd), nd[0], nd[2]];
[len(reverse(nd)), reverse(nd)[0], reverse(nd)[2]];
[len(sort(rows)), sort(rows)[0], sort(rows)[2]];
[len(sort(rows, 'desc')), sort(rows, 'desc')[0]];
[len(unique([[1, 2], [1, 2], [3, 4]])), unique([[1, 2], [1, 2], [3, 4]])[1]];
[len([take(nd, 2)]), take(nd, 2)[1]];
[len([drop(nd, 1)]), drop(nd, 1)[0]];
[len([slice(nd, 1, 3)]), slice(nd, 1, 3)[1]];
[for (r in reverse(nd)) r[0]]
