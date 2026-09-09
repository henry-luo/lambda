pn classify_u(key) int {
    if (key == "level") 1 else if (key == "service") 2 else if (key == "status") 3 else if (key == "latency") 4
    else if (key == "region") 5 else if (key == "route") 6 else if (key == "bytes") 7 else if (key == "message") 8 else 0
}
pn classify_t(key: string) int {
    if (key == "level") 1 else if (key == "service") 2 else if (key == "status") 3 else if (key == "latency") 4
    else if (key == "region") 5 else if (key == "route") 6 else if (key == "bytes") 7 else if (key == "message") 8 else 0
}
pn main() {
    let keys = split("level service status latency region route bytes message other", " ")
    var t0 = clock(); var s = 0; var i = 0
    while (i < 2000000) { s = s + classify_u(keys[i % 9]); i = i + 1 }
    print("streq untyped param 2M: " ++ string((clock() - t0) * 1000.0) ++ " ms " ++ string(s) ++ "\n")
    t0 = clock(); s = 0; i = 0
    while (i < 2000000) { s = s + classify_t(keys[i % 9]); i = i + 1 }
    print("streq string param 2M: " ++ string((clock() - t0) * 1000.0) ++ " ms " ++ string(s) ++ "\n")
}
