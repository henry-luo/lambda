// public_suffix.cpp
// Minimal public suffix list — hardcoded common TLDs and known public suffixes.
// A full PSL implementation would load from data/public_suffix_list.dat.

#include "public_suffix.h"
#include <string.h>
#include <strings.h>

// Common TLDs and well-known public suffixes
// This is a subset; a full implementation would load the Mozilla PSL.
static const char* PUBLIC_SUFFIXES[] = {
    // generic TLDs
    "com", "org", "net", "edu", "gov", "mil", "int",
    "info", "biz", "name", "pro", "aero", "coop", "museum",
    "app", "dev", "io", "ai", "co", "me", "tv", "cc", "ws",
    // country code TLDs
    "ac", "ad", "ae", "af", "ag", "al", "am", "ao", "aq", "ar",
    "as", "at", "au", "aw", "ax", "az", "ba", "bb", "bd", "be",
    "bf", "bg", "bh", "bi", "bj", "bm", "bn", "bo", "br", "bs",
    "bt", "bw", "by", "bz", "ca", "cd", "cf", "cg", "ch", "ci",
    "ck", "cl", "cm", "cn", "cr", "cu", "cv", "cw", "cx", "cy",
    "cz", "de", "dj", "dk", "dm", "do", "dz", "ec", "ee", "eg",
    "er", "es", "et", "eu", "fi", "fj", "fk", "fm", "fo", "fr",
    "ga", "gb", "gd", "ge", "gf", "gg", "gh", "gi", "gl", "gm",
    "gn", "gp", "gq", "gr", "gt", "gu", "gw", "gy", "hk", "hm",
    "hn", "hr", "ht", "hu", "id", "ie", "il", "im", "in", "iq",
    "ir", "is", "it", "je", "jm", "jo", "jp", "ke", "kg", "kh",
    "ki", "km", "kn", "kp", "kr", "kw", "ky", "kz", "la", "lb",
    "lc", "li", "lk", "lr", "ls", "lt", "lu", "lv", "ly", "ma",
    "mc", "md", "mg", "mh", "mk", "ml", "mm", "mn", "mo", "mp",
    "mq", "mr", "ms", "mt", "mu", "mv", "mw", "mx", "my", "mz",
    "na", "nc", "ne", "nf", "ng", "ni", "nl", "no", "np", "nr",
    "nu", "nz", "om", "pa", "pe", "pf", "pg", "ph", "pk", "pl",
    "pm", "pn", "pr", "ps", "pt", "pw", "py", "qa", "re", "ro",
    "rs", "ru", "rw", "sa", "sb", "sc", "sd", "se", "sg", "sh",
    "si", "sj", "sk", "sl", "sm", "sn", "so", "sr", "ss", "st",
    "su", "sv", "sx", "sy", "sz", "tc", "td", "tf", "tg", "th",
    "tj", "tk", "tl", "tm", "tn", "to", "tr", "tt", "tw", "tz",
    "ua", "ug", "uk", "us", "uy", "uz", "va", "vc", "ve", "vg",
    "vi", "vn", "vu", "wf", "ye", "yt", "za", "zm", "zw",
    // well-known second-level public suffixes
    "co.uk", "org.uk", "ac.uk", "gov.uk", "me.uk", "net.uk",
    "co.jp", "or.jp", "ne.jp", "ac.jp", "go.jp",
    "com.au", "net.au", "org.au", "edu.au", "gov.au",
    "com.br", "org.br", "net.br", "gov.br",
    "co.in", "org.in", "net.in", "gov.in", "ac.in",
    "co.nz", "net.nz", "org.nz", "govt.nz",
    "co.za", "org.za", "net.za", "gov.za",
    "com.cn", "org.cn", "net.cn", "gov.cn",
    "co.kr", "or.kr", "ne.kr",
    "com.tw", "org.tw", "net.tw", "gov.tw",
    "com.hk", "org.hk", "net.hk", "gov.hk",
    "com.sg", "org.sg", "net.sg", "gov.sg",
    "com.mx", "org.mx", "net.mx", "gob.mx",
    "com.ar", "org.ar", "net.ar", "gov.ar",
    "co.il", "org.il", "net.il", "ac.il", "gov.il",
    "co.th", "or.th", "ac.th", "go.th",
    // hosting/PaaS public suffixes
    "github.io", "herokuapp.com", "netlify.app", "vercel.app",
    "pages.dev", "workers.dev", "fly.dev",
    "azurewebsites.net", "cloudfront.net", "s3.amazonaws.com",
    "appspot.com", "firebaseapp.com", "web.app",
    "blogspot.com", "blogspot.co.uk",
    NULL
};

bool is_public_suffix(const char* domain) {
    if (!domain || !domain[0]) return false;

    for (int i = 0; PUBLIC_SUFFIXES[i]; i++) {
        if (strcasecmp(domain, PUBLIC_SUFFIXES[i]) == 0) {
            return true;
        }
    }
    return false;
}
