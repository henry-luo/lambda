// S7.4.4: error metadata does not use unrelated inferred map layouts.
fn failure(message) error => error({code: 7821, message: "wrapped: " ++ message})
fn indexed(source) map^ => if (source == null) raise error("missing") else {bytes: source}
fn from_bytes(source, expected_count = null) map^ {
    let indexed_value = indexed(source) ^ { raise failure(^.message) };
    {*:indexed_value, format_version: 1, count: expected_count}
}
fn read_message(value: any) => value.message
let good = from_bytes("data")^
let failed = from_bytes(null) ^ { ^.message }
let code = from_bytes(null) ^ { ^.code };
[good.bytes, good.format_version, good.count, failed, code,
 read_message({message: 42}), read_message({message: "ordinary"})]
