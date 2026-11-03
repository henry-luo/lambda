// D7.5.8: differential oracle uses the production strict reader and encoder.
import json: lambda.security.tuf.json
import meta: lambda.security.tuf.metadata
fn probe(row) map^ {
    let node = json.read(meta.unhex(row.hex)^) ^ { null };
    if (node == null) {id: row.id, valid: false}
    else {id: row.id, valid: true, canonical_sha256: meta.hex(hash(json.canonical(node)^, 'sha256')^),
        document: json.document(node)^}
}
pn main() {
    let cases = input("request.json", 'json')^;
    output([for (row in cases) probe(row)^], "reply.json", 'json')^
}
