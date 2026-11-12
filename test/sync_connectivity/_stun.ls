import rtc: lambda.io.rtc
pn main() {
    let channel = rtc.create({stun:["stun:127.0.0.1:" ++ sys.proc.self.env#.LAMBDA_TEST_STUN_PORT]})^
    let sdp = rtc.description(channel,"offer",5000)^
    rtc.close(channel)^
    return contains(sdp," typ srflx")^
}
