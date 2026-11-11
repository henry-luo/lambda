import crypto: lambda.io.crypto
pn main() {
    let seed = b'9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60'
    let public_key = b'd75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a'
    let signature = b'e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b'
    let alice = b'77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a'
    let bob = b'5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb'
    let alice_public = b'8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a'
    let bob_public = b'de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f'
    let shared = b'4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742'
    let low = crypto.agree(alice,b'0000000000000000000000000000000000000000000000000000000000000000') ^ { ^ }
    let short = crypto.sign(b'00',"no") ^ { ^ }
    return [crypto.public(seed,'ed25519')^ == public_key,crypto.sign(seed,null)^ == signature,
        verify_signature(public_key,null,signature,'ed25519')^,
        crypto.public(alice,'x25519')^ == alice_public,crypto.public(bob,'x25519')^ == bob_public,
        crypto.agree(alice,bob_public)^ == shared,crypto.agree(bob,alice_public)^ == shared,
        low is error,short is error]
}
