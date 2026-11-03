// RFC 8032 section 7.1, first Ed25519 vector (empty message).
let key = b'\xD75A980182B10AB7D54BFED3C964073A0EE172F3DAA62325AF021A68F707511A'
let sig = b'\xE5564300C360AC729086E2CC806E828A84877F1EB8E5D974D873E065224901555FB8821590A33BACC61E39701CF9B46BD25BF5F0595BBE24655141438E7A100B'
let good = verify_signature(key, null, sig, 'ed25519')^
let changed = verify_signature(key, "changed", sig, 'ed25519')^
let malformed = verify_signature(key, null, b'\x00', 'ed25519')^
let unsupported = verify_signature(key, null, sig, 'unknown') ^ { 'denied' }
let wrong_type = verify_signature(7, null, sig, 'ed25519') ^ { 'denied' };
[good, changed, malformed, unsupported, wrong_type]
