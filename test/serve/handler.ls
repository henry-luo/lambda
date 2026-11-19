pub pn handle(request) any^ {
    if (request.path == "/binary") {
        return {status:200, headers:{'Content-Type':"application/octet-stream"}, body:b'\x00FF41'}
    }
    if (request.path == "/failure") { raise error("expected test failure") }
    if (request.path == "/bad-header") {
        return {status:200, headers:{'X-Test':"ok\r\nX-Injected: bad"}, body:"bad"}
    }
    return {status:201, headers:{'Content-Type':"application/json"},
        body:format({method:request.method,path:request.path,body:request.body}, 'json')^}
}
