// v5 §5.6: `index` stays an i64 only while it is a native loop counter.
// Boxing it for the JS property protocol must produce a float Item, never an
// LMD_TYPE_INT Item that could be mistaken for a Symbol.
const values = [10, 20];
for (const index in values) {
    console.log(index);
}
