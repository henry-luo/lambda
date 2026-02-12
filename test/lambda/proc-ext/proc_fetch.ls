pn main() {
    let resp = fetch("http://example.com", {"method": "GET"})?;
    print("<h1>Example Domain</h1>" in resp);
}