import s3: lambda.sync.s3
import hex: lambda.security.tuf.metadata
pn main() {
    let config = {access_key:"AKIAIOSFODNN7EXAMPLE",secret_key:"wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY",region:"us-east-1"}
    let empty = hex.hex(hash(null,'sha256')^)^
    let signed = s3.signed(config,"GET","/test.txt",{},
        {host:"examplebucket.s3.amazonaws.com",range:"bytes=0-9"},"20130524T000000Z",empty)^
    let rejected = s3.validate({*:config,endpoint:"http://example.com",bucket:"bucket"}) ^ { ^ }
    let long_key = binary([for (i in 1 to 131) 170])^
    return [hex.hex(s3.hmac("key","The quick brown fox jumps over the lazy dog")^)^ ==
        "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8",
        hex.hex(s3.hmac(long_key,"Test Using Larger Than Block-Size Key - Hash Key First")^)^ ==
        "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54",
        ends_with(signed.authorization,"f0e8bdb87c964420e857bd35b5d6ed310bd44f0170aba48dd91039c6036bdb41"),
        s3.stamp(1369353600000)^ == "20130524T000000Z",
        s3.checksum("abc")^ == "ungWv48Bz+pBQUDeXa4iI7ADYaOWF3qctBD/YfIAFa0=",rejected is error]
}
