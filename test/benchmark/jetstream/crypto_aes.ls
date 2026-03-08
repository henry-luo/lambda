// JetStream Benchmark: crypto-aes (SunSpider)
// AES cipher in Counter mode (CTR)
// Tests bitwise operations, array manipulation
//
// Uses FLAT arrays (no nested arrays) to work around a Lambda GC bug
// that corrupts inner array references during compaction.

// Sbox is pre-computed multiplicative inverse in GF(2^8) [§5.1.1]
let Sbox = [
     99,124,119,123,242,107,111,197, 48,  1,103, 43,254,215,171,118,
    202,130,201,125,250, 89, 71,240,173,212,162,175,156,164,114,192,
    183,253,147, 38, 54, 63,247,204, 52,165,229,241,113,216, 49, 21,
      4,199, 35,195, 24,150,  5,154,  7, 18,128,226,235, 39,178,117,
      9,131, 44, 26, 27,110, 90,160, 82, 59,214,179, 41,227, 47,132,
     83,209,  0,237, 32,252,177, 91,106,203,190, 57, 74, 76, 88,207,
    208,239,170,251, 67, 77, 51,133, 69,249,  2,127, 80, 60,159,168,
     81,163, 64,143,146,157, 56,245,188,182,218, 33, 16,255,243,210,
    205, 12, 19,236, 95,151, 68, 23,196,167,126, 61,100, 93, 25,115,
     96,129, 79,220, 34, 42,144,136, 70,238,184, 20,222, 94, 11,219,
    224, 50, 58, 10, 73,  6, 36, 92,194,211,172, 98,145,149,228,121,
    231,200, 55,109,141,213, 78,169,108, 86,244,234,101,122,174,  8,
    186,120, 37, 46, 28,166,180,198,232,221,116, 31, 75,189,139,138,
    112, 62,181,102, 72,  3,246, 14, 97, 53, 87,185,134,193, 29,158,
    225,248,152, 17,105,217,142,148,155, 30,135,233,206, 85, 40,223,
    140,161,137, 13,191,230, 66,104, 65,153, 45, 15,176, 84,187, 22
]

// Rcon flat: 11 * 4 = 44 entries. Rcon[i][j] -> RconFlat[i*4+j]
let RconFlat = [
    0,0,0,0, 1,0,0,0, 2,0,0,0, 4,0,0,0, 8,0,0,0, 16,0,0,0,
    32,0,0,0, 64,0,0,0, 128,0,0,0, 27,0,0,0, 54,0,0,0
]

// ================= AES Core Functions (flat state: state[row*4+col]) =================

pn SubBytes(s) {
    var i: int = 0
    while (i < 16) {
        s[i] = Sbox[s[i]]
        i = i + 1
    }
    return s
}

pn ShiftRows(s) {
    // Row 1: shift left by 1
    var t = s[1]
    s[1] = s[5]
    s[5] = s[9]
    s[9] = s[13]
    s[13] = t
    // Row 2: shift left by 2
    t = s[2]
    s[2] = s[10]
    s[10] = t
    t = s[6]
    s[6] = s[14]
    s[14] = t
    // Row 3: shift left by 3 = shift right by 1
    t = s[15]
    s[15] = s[11]
    s[11] = s[7]
    s[7] = s[3]
    s[3] = t
    return s
}

pn MixColumns(s) {
    var c: int = 0
    while (c < 4) {
        var a0 = s[0 + c * 4]
        var a1 = s[1 + c * 4]
        var a2 = s[2 + c * 4]
        var a3 = s[3 + c * 4]
        var b0 = shl(a0, 1)
        var b1 = shl(a1, 1)
        var b2 = shl(a2, 1)
        var b3 = shl(a3, 1)
        if (band(a0, 128) != 0) { b0 = bxor(b0, 283) }
        if (band(a1, 128) != 0) { b1 = bxor(b1, 283) }
        if (band(a2, 128) != 0) { b2 = bxor(b2, 283) }
        if (band(a3, 128) != 0) { b3 = bxor(b3, 283) }
        s[0 + c * 4] = bxor(bxor(bxor(b0, a1), b1), bxor(a2, a3))
        s[1 + c * 4] = bxor(bxor(bxor(a0, b1), a2), bxor(b2, a3))
        s[2 + c * 4] = bxor(bxor(bxor(a0, a1), b2), bxor(a3, b3))
        s[3 + c * 4] = bxor(bxor(bxor(a0, b0), a1), bxor(a2, b3))
        c = c + 1
    }
    return s
}

// w is flat: w[(rnd*4+c)*4+r]
pn AddRoundKey(state, w, rnd: int) {
    var r: int = 0
    while (r < 4) {
        var c: int = 0
        while (c < 4) {
            state[r + c * 4] = bxor(state[r + c * 4], w[(rnd * 4 + c) * 4 + r])
            c = c + 1
        }
        r = r + 1
    }
    return state
}

// Main Cipher function [§5.1]
// input: flat 16-byte array. w: flat key schedule (total_words * 4 bytes)
pn AesCipher(input, w) {
    var Nr = int(len(w) / 16) - 1

    // State is flat: state[row + col*4] (column-major to match AES spec)
    var state = fill(16, 0)
    var i: int = 0
    while (i < 16) {
        // input is in column-major order: input[row + col*4]
        state[i] = input[i]
        i = i + 1
    }

    state = AddRoundKey(state, w, 0)

    var round: int = 1
    while (round < Nr) {
        state = SubBytes(state)
        state = ShiftRows(state)
        state = MixColumns(state)
        state = AddRoundKey(state, w, round)
        round = round + 1
    }

    state = SubBytes(state)
    state = ShiftRows(state)
    state = AddRoundKey(state, w, Nr)

    return state
}

// Key Expansion [§5.2] - returns flat array (total_words * 4 bytes)
pn KeyExpansion(key) {
    var Nk = int(len(key) / 4)
    var Nr = Nk + 6
    var total = 4 * (Nr + 1)  // total words

    var w = fill(total * 4, 0)
    var temp = fill(4, 0)

    // Copy key into first Nk words
    var i: int = 0
    while (i < Nk) {
        w[i * 4 + 0] = key[4 * i + 0]
        w[i * 4 + 1] = key[4 * i + 1]
        w[i * 4 + 2] = key[4 * i + 2]
        w[i * 4 + 3] = key[4 * i + 3]
        i = i + 1
    }

    i = Nk
    while (i < total) {
        // temp = w[i-1]
        var t: int = 0
        while (t < 4) {
            temp[t] = w[(i - 1) * 4 + t]
            t = t + 1
        }
        if (i % Nk == 0) {
            // RotWord
            var t0 = temp[0]
            temp[0] = temp[1]
            temp[1] = temp[2]
            temp[2] = temp[3]
            temp[3] = t0
            // SubWord
            t = 0
            while (t < 4) {
                temp[t] = Sbox[temp[t]]
                t = t + 1
            }
            // XOR with Rcon
            var ri = int(i / Nk)
            t = 0
            while (t < 4) {
                temp[t] = bxor(temp[t], RconFlat[ri * 4 + t])
                t = t + 1
            }
        } else if (Nk > 6 and i % Nk == 4) {
            // SubWord
            t = 0
            while (t < 4) {
                temp[t] = Sbox[temp[t]]
                t = t + 1
            }
        }
        t = 0
        while (t < 4) {
            w[i * 4 + t] = bxor(w[(i - Nk) * 4 + t], temp[t])
            t = t + 1
        }
        i = i + 1
    }
    return w
}

// ================= Helpers =================

pn concat_arr(a, b) {
    var la = int(len(a))
    var lb = int(len(b))
    var result = fill(la + lb, 0)
    var i: int = 0
    while (i < la) {
        result[i] = a[i]
        i = i + 1
    }
    i = 0
    while (i < lb) {
        result[la + i] = b[i]
        i = i + 1
    }
    return result
}

// Build a fresh counter block for CTR mode
pn make_counter_block(nonce: int, nonce_hi: int, b: int) {
    var cb = fill(16, 0)
    var i: int = 0
    while (i < 4) {
        cb[i] = band(shr(nonce, i * 8), 255)
        i = i + 1
    }
    i = 0
    while (i < 4) {
        cb[i + 4] = band(shr(nonce_hi, i * 8), 255)
        i = i + 1
    }
    var c: int = 0
    while (c < 4) {
        cb[15 - c] = band(shr(b, c * 8), 255)
        c = c + 1
    }
    return cb
}

// Derive AES-256 key schedule from password
pn derive_key_schedule(password, nBits: int) {
    var nBytes = int(nBits / 8)
    var pwBytes = fill(nBytes, 0)
    var i: int = 0
    while (i < nBytes) {
        pwBytes[i] = band(int(ord(slice(password, i, i + 1))), 255)
        i = i + 1
    }
    var key = AesCipher(pwBytes, KeyExpansion(pwBytes))
    key = concat_arr(key, slice(key, 0, nBytes - 16))
    return KeyExpansion(key)
}

// ================= Benchmark =================
// Encrypt plaintext, then decrypt and verify byte-by-byte

pn run() {
    var plainText = "ROMEO: But, soft! what light through yonder window breaks?\nIt is the east, and Juliet is the sun.\nArise, fair sun, and kill the envious moon,\nWho is already sick and pale with grief,\nThat thou her maid art far more fair than she:\nBe not her maid, since she is envious;\nHer vestal livery is but sick and green\nAnd none but fools do wear it; cast it off.\nIt is my lady, O, it is my love!\nO, that she knew she were!\nShe speaks yet she says nothing: what of that?\nHer eye discourses; I will answer it.\nI am too bold, 'tis not to me she speaks:\nTwo of the fairest stars in all the heaven,\nHaving some business, do entreat her eyes\nTo twinkle in their spheres till they return.\nWhat if her eyes were there, they in her head?\nThe brightness of her cheek would shame those stars,\nAs daylight doth a lamp; her eyes in heaven\nWould through the airy region stream so bright\nThat birds would sing and think it were not night.\nSee, how she leans her cheek upon her hand!\nO, that I were a glove upon that hand,\nThat I might touch that cheek!\nJULIET: Ay me!\nROMEO: She speaks:\nO, speak again, bright angel! for thou art\nAs glorious to this night, being o'er my head\nAs is a winged messenger of heaven\nUnto the white-upturned wondering eyes\nOf mortals that fall back to gaze on him\nWhen he bestrides the lazy-pacing clouds\nAnd sails upon the bosom of the air."

    var password = "O Romeo, Romeo! wherefore art thou Romeo?"
    var nBits: int = 256
    var blockSize: int = 16
    var nonce: int = 1311788257000
    var nonce_hi = int(nonce / 4294967296)

    // Derive key
    var keySchedule = derive_key_schedule(password, nBits)

    // Convert plaintext to byte array
    var pt_len = int(len(plainText))
    var pt_bytes = fill(pt_len, 0)
    var i: int = 0
    while (i < pt_len) {
        pt_bytes[i] = int(ord(slice(plainText, i, i + 1)))
        i = i + 1
    }

    var blockCount = int(ceil(pt_len / blockSize))

    // ========= ENCRYPT =========
    var ct_bytes = fill(pt_len, 0)
    var b: int = 0
    while (b < blockCount) {
        var cb = make_counter_block(nonce, nonce_hi, b)
        var cipherCntr = AesCipher(cb, keySchedule)

        var blockLength: int = blockSize
        if (b == blockCount - 1) {
            blockLength = (pt_len - 1) % blockSize + 1
        }

        i = 0
        while (i < blockLength) {
            ct_bytes[b * blockSize + i] = bxor(pt_bytes[b * blockSize + i], cipherCntr[i])
            i = i + 1
        }
        b = b + 1
    }

    // ========= DECRYPT and VERIFY =========
    b = 0
    while (b < blockCount) {
        var cb = make_counter_block(nonce, nonce_hi, b)
        var cipherCntr = AesCipher(cb, keySchedule)

        var blockLength: int = blockSize
        if (b == blockCount - 1) {
            blockLength = (pt_len - 1) % blockSize + 1
        }

        i = 0
        while (i < blockLength) {
            var pos = b * blockSize + i
            var decByte = bxor(ct_bytes[pos], cipherCntr[i])
            if (decByte != pt_bytes[pos]) {
                print("crypto-aes: FAIL at pos " ++ string(pos) ++ "\n")
                return false
            }
            i = i + 1
        }
        b = b + 1
    }

    return true
}

pn main() {
    var __t0 = clock()
    var pass = true
    var iter: int = 0
    while (iter < 40) {
        if (run() == false) {
            pass = false
        }
        iter = iter + 1
    }
    var __t1 = clock()
    if (pass) {
        print("crypto-aes: PASS\n")
    } else {
        print("crypto-aes: FAIL\n")
    }
    print("__TIMING__:" ++ string((__t1 - __t0) * 1000.0) ++ "\n")
}
