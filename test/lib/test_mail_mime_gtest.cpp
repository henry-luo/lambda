// test_mail_mime_gtest.cpp — MIME header decoding (lib/mail_mime;
// vibe/Lambda_Demo_Mail.md MG5).

#include <gtest/gtest.h>

#include <string>
#include <string.h>
#include <vector>

#include "../../lib/mail_mime.h"
#include "../../lib/strbuf.h"

namespace {

std::string to_utf8(const char* charset, const std::string& bytes) {
    StrBuf* out = strbuf_new();
    mime_to_utf8(charset, charset ? strlen(charset) : 0, bytes.data(), bytes.size(), out);
    std::string s(out->str, out->length);
    strbuf_free(out);
    return s;
}

std::string header(const std::string& value) {
    StrBuf* out = strbuf_new();
    mime_decode_header(value.data(), value.size(), out);
    std::string s(out->str, out->length);
    strbuf_free(out);
    return s;
}

std::string param(const std::string& value, const char* name, bool decode_words = true) {
    StrBuf* out = strbuf_new();
    bool found = mime_header_param(value.data(), value.size(), name, decode_words, out);
    std::string s = found ? std::string(out->str, out->length) : std::string("<absent>");
    strbuf_free(out);
    return s;
}

std::vector<std::string> addresses(const std::string& value) {
    std::vector<std::string> list;
    mime_parse_addresses(value.data(), value.size(),
        [](void* user, const char* name, size_t name_length, const char* address, size_t address_length) {
            auto* l = (std::vector<std::string>*)user;
            l->push_back(std::string(name, name_length) + " <" + std::string(address, address_length) + ">");
        }, &list);
    return list;
}

}  // namespace

TEST(MailMimeCharset, FastPathsAndReplacement) {
    EXPECT_EQ(to_utf8("UTF-8", "caf\xc3\xa9"), "caf\xc3\xa9");
    EXPECT_EQ(to_utf8("utf-8", "bad\xff!"), "bad\xef\xbf\xbd!");
    // latin-1 labels decode as windows-1252 (WHATWG)
    EXPECT_EQ(to_utf8("ISO-8859-1", "\x80 \xe9"), "\xe2\x82\xac \xc3\xa9");
    EXPECT_EQ(to_utf8(NULL, "plain"), "plain");
}

TEST(MailMimeCharset, IconvAndAliases) {
    EXPECT_EQ(to_utf8("KOI8-R", "\xf0\xd2\xc9\xd7\xc5\xd4"), "\xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82");
    // gb2312 text decodes through GB18030
    EXPECT_EQ(to_utf8("gb2312", "\xc4\xe3\xba\xc3"), "\xe4\xbd\xa0\xe5\xa5\xbd");
    EXPECT_EQ(to_utf8("x-no-such-charset", "ascii"), "ascii");
}

TEST(MailMimeHeader, EncodedWords) {
    EXPECT_EQ(header("=?ISO-8859-1?Q?Caf=E9_cr=E8me?="), "Caf\xc3\xa9 cr\xc3\xa8me");
    EXPECT_EQ(header("=?utf-8?b?w4lsaXNl?= Dupr=?UTF-8?Q?=C3=A9?="), "\xc3\x89lise Dupr\xc3\xa9");
    // whitespace between two encoded words is dropped (RFC 2047 §6.2)
    EXPECT_EQ(header("=?UTF-8?Q?a?=  \t =?UTF-8?Q?b?="), "ab");
    EXPECT_EQ(header("x =?UTF-8?Q?a?= y"), "x a y");
    // one multibyte character split across two words
    EXPECT_EQ(header("=?UTF-8?B?5pe=?= =?UTF-8?B?pQ==?="), "\xe6\x97\xa5");
    // the RFC 2231 language suffix on the charset
    EXPECT_EQ(header("=?UTF-8*en?Q?hi?="), "hi");
}

TEST(MailMimeHeader, MalformedStaysText) {
    EXPECT_EQ(header("=?UTF-8?Q?unterminated"), "=?UTF-8?Q?unterminated");
    EXPECT_EQ(header("=?UTF-8?X?bad?="), "=?UTF-8?X?bad?=");
    EXPECT_EQ(header("=?UTF-8?B??="), "");
    // raw 8-bit text that is not UTF-8 reads as windows-1252
    EXPECT_EQ(header("J\xf6rg"), "J\xc3\xb6rg");
}

TEST(MailMimeParam, PlainQuotedAndTokens) {
    std::string ct = "multipart/mixed; boundary=\"=?outer?=\"; charset=UTF-8";
    EXPECT_EQ(param(ct, "boundary", false), "=?outer?=");
    EXPECT_EQ(param(ct, "charset", false), "UTF-8");
    EXPECT_EQ(param(ct, "name"), "<absent>");
    EXPECT_EQ(param("attachment; filename=\"a \\\"b\\\".txt\"", "filename"), "a \"b\".txt");
    StrBuf* token = strbuf_new();
    mime_header_token(" Text/HTML ; charset=x", 22, token);
    EXPECT_EQ(std::string(token->str, token->length), "text/html");
    strbuf_free(token);
}

TEST(MailMimeParam, Rfc2231) {
    EXPECT_EQ(param("attachment; filename*=UTF-8''na%C3%AFve.txt", "filename"), "na\xc3\xafve.txt");
    EXPECT_EQ(param("attachment; filename*0*=UTF-8''Re%C3%A7u; filename*1*=%20n%C2%B0%2042.pdf", "filename"),
              "Re\xc3\xa7u n\xc2\xb0 42.pdf");
    // unencoded continuations, out of order
    EXPECT_EQ(param("x; name*1=\"two\"; name*0=\"one-\"", "name"), "one-two");
    // a stray RFC 2047 word in a quoted filename
    EXPECT_EQ(param("attachment; filename=\"=?UTF-8?B?w6kucGRm?=\"", "filename"), "\xc3\xa9.pdf");
}

TEST(MailMimeAddresses, Forms) {
    auto list = addresses("\"Doe, John\" <john@example.com>, ada@example.org (Ada Lovelace), "
                          "Team: bob@example.com, \"Carol \\\"CJ\\\" Jones\" <carol@example.com>;");
    ASSERT_EQ(list.size(), 4u);
    EXPECT_EQ(list[0], "Doe, John <john@example.com>");
    EXPECT_EQ(list[1], "Ada Lovelace <ada@example.org>");
    EXPECT_EQ(list[2], " <bob@example.com>");
    EXPECT_EQ(list[3], "Carol \"CJ\" Jones <carol@example.com>");
}

TEST(MailMimeAddresses, ParenthesesInQuotedName) {
    // inside quotes "(" is text; outside it opens a comment that is dropped
    auto list = addresses("\"Ann Lee (Forge)\" <n@forge.example>, Bo (work) <bo@example.com>");
    ASSERT_EQ(list.size(), 2u);
    EXPECT_EQ(list[0], "Ann Lee (Forge) <n@forge.example>");
    EXPECT_EQ(list[1], "Bo <bo@example.com>");
}

TEST(MailMimeAddresses, EncodedNamesGroupsAndRoutes) {
    auto list = addresses("=?ISO-8859-1?Q?J=F6rg?= <joerg@example.de>, undisclosed-recipients:;, <@relay:x@y.org>");
    ASSERT_EQ(list.size(), 2u);
    EXPECT_EQ(list[0], "J\xc3\xb6rg <joerg@example.de>");
    EXPECT_EQ(list[1], " <x@y.org>");
    EXPECT_TRUE(addresses("").empty());
    EXPECT_TRUE(addresses(" , ;").empty());
}
