// RFC 6979 A.2.5 P-256/SHA-256 (low-S), and RFC 7748 section 6.1 X25519.
import crypto: lambda.io.crypto
pn main() {
    let private_key = b'c9afa9d845ba75166b5c215767b1d6934e50c3db36e89b127b8a622b120f6721'
    let public_key = b'0460fed4ba255a9d31c961eb74c6356d68c049b8923b61fa6ce669622e60f29fb67903fe1008b8bc99a41ae9e95628bc64f2f1b20c2d7e9f5177a3c294d4462299'
    let sample = b'efd48b2aacb6a8fd1140dd9cd45e81d69d2c877b56aaf991c34d0ea84eaf37160834e36ad29a83bf2bc9385e491d6099c8fdf9d1ed67aa7ea5f51f93782857a9'
    let test = b'f1abb023518351cd71d881567b1ea663ed3efcf6c5132b354f28d3b0b7d38367019f4113742a2b14bd25926b49c649155f267e60d3814b4c0cc84250e46f0083'
    let alice = b'77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a'
    let bob = b'5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb'
    let alice_public = b'8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a'
    let bob_public = b'de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f'
    let shared = b'4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742'
    let zero = b'0000000000000000000000000000000000000000000000000000000000000000'
    let low = crypto.agree(alice,zero) ^ { ^ }
    let low_one = crypto.agree(alice,b'0100000000000000000000000000000000000000000000000000000000000000') ^ { ^ }
    let invalid_scalar = crypto.public(zero,'ecdsa-sha2-nistp256') ^ { ^ }
    let order = crypto.public(b'ffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551','ecdsa-sha2-nistp256') ^ { ^ }
    let short = crypto.sign(b'00',"no") ^ { ^ }
    let legacy = crypto.public(private_key,'ed25519') ^ { ^ }
    let wrong_message = crypto.sign(private_key,7) ^ { ^ }
    return [crypto.public(private_key,'ecdsa-sha2-nistp256')^ == public_key,
        crypto.sign(private_key,"sample")^ == sample,crypto.sign(private_key,"test")^ == test,
        crypto.sign(private_key,"sample")^ == crypto.sign(private_key,"sample")^,
        crypto.public(alice,'x25519')^ == alice_public,crypto.public(bob,'x25519')^ == bob_public,
        crypto.agree(alice,bob_public)^ == shared,crypto.agree(bob,alice_public)^ == shared,
        low is error,low_one is error,invalid_scalar is error,order is error,short is error,
        legacy is error,wrong_message is error,len(crypto.sign(private_key,null)^)^ == 64]
}
