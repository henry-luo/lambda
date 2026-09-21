// S2.4.2v5: an integer path step must fit an IntKey; overflow is a compile
// error, never a silently wrong path.
let p = \.99999999999999999999;
[p]
