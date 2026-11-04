// D11.1.2, D11.2.4: exercise production URI policy without granting IO authority.
import uri: lambda.security.tuf.path
fn probe(row) map {
    {id: row.id, role: uri.role_name(row.text), wire: uri.repository_path(row.text), target: uri.target_name(row.text),
     component: uri.component(row.text) ^ { null }, encoded_target: uri.target_path(row.text) ^ { null }}
}
pn main() {
    let cases = input("request.json", 'json')^;
    output([for (row in cases) probe(row)], "reply.json", 'json')^
}
