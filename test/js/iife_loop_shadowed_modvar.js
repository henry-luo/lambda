(() => {
    // A direct IIFE var is promoted for nested closures, but a loop lexical
    // must retain its own binding when the names coincide.
    var i;
    for (i = 1; i <= 91; i++) {}

    class Tile {}
    class LineTile extends Tile {}
    const buckets = [Tile, LineTile];
    for (let i = 0; i < buckets.length; i++)
        buckets[i].bucket = i;

    console.log(Tile.bucket);
    console.log(LineTile.bucket);
})();
