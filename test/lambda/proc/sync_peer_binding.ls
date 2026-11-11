import ws: lambda.io.websocket
pn main() {
    let hostname = ws.listen(0,{bind:"localhost"}) ^ { ^ }
    let embedded = ws.listen(0,{bind:"127.0.0.1\u0000wrong"}) ^ { ^ }
    let invalid = ws.listen(0,{bind:"300.1.1.1"}) ^ { ^ }
    let listener = ws.listen(0,{bind:"127.0.0.1"})^
    let selected = listener.port > 0
    ws.close(listener)^
    return [hostname is error,embedded is error,invalid is error,selected]
}
