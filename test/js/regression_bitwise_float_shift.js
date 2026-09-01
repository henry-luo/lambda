class Tree {
    static from(parts, total = parts.reduce((sum, part) => sum + part.length, 0)) {
        let size = Math.max(32, total >> 5);
        let doubled = size << 1;
        let halved = size >> 1;
        function retain(value) { return value >= doubled ? value : halved; }
        return retain(doubled) + halved;
    }
}

console.log(Tree.from([{length: 64}], 64));
