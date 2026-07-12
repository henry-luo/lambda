pn main() {
    let resp = fetch("test/input/test_data.html", {'method': "GET"})^;
    print("<h1>Welcome</h1>" in resp);
}
