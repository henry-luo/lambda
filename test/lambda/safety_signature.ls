// RFC 6979 A.2.5 P-256/SHA-256, canonical low-S r||s profile.
let key = b'0460fed4ba255a9d31c961eb74c6356d68c049b8923b61fa6ce669622e60f29fb67903fe1008b8bc99a41ae9e95628bc64f2f1b20c2d7e9f5177a3c294d4462299'
let sig = b'efd48b2aacb6a8fd1140dd9cd45e81d69d2c877b56aaf991c34d0ea84eaf37160834e36ad29a83bf2bc9385e491d6099c8fdf9d1ed67aa7ea5f51f93782857a9'
let good = verify_signature(key, "sample", sig, 'ecdsa-sha2-nistp256')^
let changed = verify_signature(key, "changed", sig, 'ecdsa-sha2-nistp256')^
let malformed = verify_signature(key, "sample", b'00', 'ecdsa-sha2-nistp256')^
let unsupported = verify_signature(key, "sample", sig, 'unknown') ^ { 'denied' }
let legacy = verify_signature(key, "sample", sig, 'ed25519') ^ { 'denied' }
let wrong_type = verify_signature(7, "sample", sig, 'ecdsa-sha2-nistp256') ^ { 'denied' };
[good, changed, malformed, unsupported, legacy, wrong_type]
