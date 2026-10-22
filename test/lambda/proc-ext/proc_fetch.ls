pn main() {
    let resp = fetch("test/input/test_data.html", {'method': "GET"})^;
    print(contains(resp.body, "<h1>Welcome</h1>") and resp.status == 200)
}
