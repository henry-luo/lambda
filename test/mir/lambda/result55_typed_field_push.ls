// D5.4.4/S9.1.2: a typed array field may append through its guarded place.
type Bucket = {values: int[]}

pn append(var bucket: Bucket, value: int) any {
    push(bucket.values, value)^
}

pn main() {
    let original: Bucket = {values: [1]}^
    var bucket: Bucket = original^
    let snapshot = bucket
    var i: int = 2
    while (i <= 19) {
        append(bucket, i)^
        i = i + 1
    }
    print([snapshot.values, bucket.values[0], len(bucket.values), bucket.values[18]])
    print("\n")
}
