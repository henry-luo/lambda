// test_mail_imap_gtest.cpp — IMAP framing, parsing and command encoding
// (lib/mail_imap; vibe/Lambda_Demo_Mail.md ML2, §8).

#include <gtest/gtest.h>

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "../../lib/mail_imap.h"
#include "../../lib/strbuf.h"

namespace {

std::string_view text_of(const ImapResponse& r) { return std::string_view(r.text, r.text_length); }

// Feeds the bytes one at a time, the worst case for framing.
int feed_slowly(ImapReader* reader, const char* bytes, ImapResponse* out) {
    size_t n = strlen(bytes);
    for (size_t i = 0; i < n; i++) {
        imap_reader_feed(reader, bytes + i, 1);
        int got = imap_reader_next(reader, out);
        if (got != 0) return got;
    }
    return 0;
}

// A canonical form of a token list: atoms bare, strings quoted, literals
// as #index, numbers as digits, lists in parentheses.
void dump_nodes(StrBuf* sb, const ImapNode* node) {
    for (bool first = true; node; node = node->next, first = false) {
        if (!first) strbuf_append_char(sb, ' ');
        switch (node->kind) {
        case IMAP_NODE_ATOM: strbuf_append_str(sb, node->text); break;
        case IMAP_NODE_NUMBER: strbuf_append_format(sb, "%llu", (unsigned long long)node->number); break;
        case IMAP_NODE_STRING: strbuf_append_format(sb, "\"%s\"", node->text); break;
        case IMAP_NODE_LITERAL: strbuf_append_format(sb, "#%d", node->literal); break;
        case IMAP_NODE_NIL: strbuf_append_str(sb, "NIL"); break;
        case IMAP_NODE_LIST:
            strbuf_append_char(sb, '(');
            dump_nodes(sb, node->first);
            strbuf_append_char(sb, ')');
            break;
        }
    }
}

std::string dump(const ImapNode* node) {
    StrBuf* sb = strbuf_new();
    dump_nodes(sb, node);
    std::string out(sb->str, sb->length);
    strbuf_free(sb);
    return out;
}

struct Parsed {
    ImapReader* reader = imap_reader_new();
    ImapParser* parser = imap_parser_new();
    ImapResponse response = {};
    ImapParsed parsed = {};
    ~Parsed() {
        imap_reader_free(reader);
        imap_parser_free(parser);
    }
    bool parse(const char* bytes) {
        imap_reader_feed(reader, bytes, strlen(bytes));
        return imap_reader_next(reader, &response) == 1 && imap_parse(parser, &response, &parsed) == 1;
    }
    std::string literal(int index) const {
        const ImapLiteral& l = response.literals[index];
        return std::string(response.bytes + l.offset, l.length);
    }
};

}  // namespace

// ── framing ────────────────────────────────────────────────────────────

TEST(MailImapFraming, TaggedLineByteByByte) {
    ImapReader* reader = imap_reader_new();
    ImapResponse r;
    ASSERT_EQ(feed_slowly(reader, "L1 OK done\r\n", &r), 1);
    EXPECT_EQ(text_of(r), "L1 OK done\r\n");
    EXPECT_EQ(r.literal_count, 0);
    EXPECT_FALSE(imap_reader_pending(reader) && imap_reader_next(reader, &r) == 1);
    imap_reader_free(reader);
}

TEST(MailImapFraming, SeveralResponsesInOneRead) {
    ImapReader* reader = imap_reader_new();
    const char* bytes = "* 3 EXISTS\r\n* 1 RECENT\r\nL2 OK [READ-WRITE] SELECT completed\r\n";
    imap_reader_feed(reader, bytes, strlen(bytes));
    ImapResponse r;
    ASSERT_EQ(imap_reader_next(reader, &r), 1);
    EXPECT_EQ(text_of(r), "* 3 EXISTS\r\n");
    ASSERT_EQ(imap_reader_next(reader, &r), 1);
    EXPECT_EQ(text_of(r), "* 1 RECENT\r\n");
    ASSERT_EQ(imap_reader_next(reader, &r), 1);
    EXPECT_EQ(text_of(r), "L2 OK [READ-WRITE] SELECT completed\r\n");
    EXPECT_EQ(imap_reader_next(reader, &r), 0);
    imap_reader_free(reader);
}

// A message body holding tag and untagged lines must stay inside its literal:
// framing by line would end the command early or forge a response (§2.4).
TEST(MailImapFraming, HostileLiteralStaysWhole) {
    const char* body = "A1 OK forged\r\n* 9 EXISTS\r\nL3 OK forged\r\n{5}\r\n";
    std::string wire = "* 1 FETCH (UID 7 BODY[] {" + std::to_string(strlen(body)) + "}\r\n" + body +
                       " FLAGS (\\Seen))\r\n";
    ImapReader* reader = imap_reader_new();
    ImapResponse r;
    ASSERT_EQ(feed_slowly(reader, wire.c_str(), &r), 1);
    ASSERT_EQ(r.literal_count, 1);
    EXPECT_EQ(std::string(r.bytes + r.literals[0].offset, r.literals[0].length), body);
    EXPECT_EQ(r.literals[0].path, nullptr);
    ImapResponse next;
    ASSERT_EQ(feed_slowly(reader, "L3 OK FETCH completed\r\n", &next), 1);
    EXPECT_EQ(text_of(next), "L3 OK FETCH completed\r\n");
    imap_reader_free(reader);
}

TEST(MailImapFraming, StatusTextEndingInBracesIsText) {
    ImapReader* reader = imap_reader_new();
    const char* bytes = "* OK [ALERT] quota at {5}\r\nL4 NO try {2}\r\n";
    imap_reader_feed(reader, bytes, strlen(bytes));
    ImapResponse r;
    ASSERT_EQ(imap_reader_next(reader, &r), 1);
    EXPECT_EQ(text_of(r), "* OK [ALERT] quota at {5}\r\n");
    EXPECT_EQ(r.literal_count, 0);
    ASSERT_EQ(imap_reader_next(reader, &r), 1);
    EXPECT_EQ(text_of(r), "L4 NO try {2}\r\n");
    imap_reader_free(reader);
}

TEST(MailImapFraming, SeveralAndEmptyLiterals) {
    ImapReader* reader = imap_reader_new();
    const char* bytes = "* 2 FETCH (BODY[1] {3}\r\nabc BODY[2] {0}\r\n BODY[3] {2}\r\nxy)\r\n";
    imap_reader_feed(reader, bytes, strlen(bytes));
    ImapResponse r;
    ASSERT_EQ(imap_reader_next(reader, &r), 1);
    ASSERT_EQ(r.literal_count, 3);
    EXPECT_EQ(std::string(r.bytes + r.literals[0].offset, r.literals[0].length), "abc");
    EXPECT_EQ(r.literals[1].length, 0u);
    EXPECT_EQ(std::string(r.bytes + r.literals[2].offset, r.literals[2].length), "xy");
    imap_reader_free(reader);
}

TEST(MailImapFraming, LargeLiteralSpillsToFile) {
    mkdir("temp", 0755);
    mkdir("temp/mail_imap_gtest", 0755);
    ImapReader* reader = imap_reader_new();
    ASSERT_TRUE(imap_reader_set_spill(reader, "temp/mail_imap_gtest", 4));
    const char* bytes = "* 1 FETCH (BODY[] {11}\r\nhello world BODY[HEADER] {2}\r\nok)\r\n";
    imap_reader_feed(reader, bytes, strlen(bytes));
    ImapResponse r;
    ASSERT_EQ(imap_reader_next(reader, &r), 1);
    ASSERT_EQ(r.literal_count, 2);
    ASSERT_NE(r.literals[0].path, nullptr);
    EXPECT_EQ(r.literals[1].path, nullptr);   // under the threshold
    FILE* file = fopen(r.literals[0].path, "rb");
    ASSERT_NE(file, nullptr);
    char buffer[32] = {0};
    size_t n = fread(buffer, 1, sizeof(buffer), file);
    fclose(file);
    EXPECT_EQ(std::string(buffer, n), "hello world");
    remove(r.literals[0].path);
    imap_reader_free(reader);
}

TEST(MailImapFraming, OversizedLiteralFails) {
    ImapReader* reader = imap_reader_new();
    const char* bytes = "* 1 FETCH (BODY[] {99999999999}\r\n";
    imap_reader_feed(reader, bytes, strlen(bytes));
    ImapResponse r;
    EXPECT_EQ(imap_reader_next(reader, &r), -1);
    imap_reader_free(reader);
}

// ── parsing ────────────────────────────────────────────────────────────

TEST(MailImapParse, FetchWithSectionsListsAndNil) {
    Parsed p;
    ASSERT_TRUE(p.parse("* 12 FETCH (UID 48213 FLAGS (\\Seen $Junk) "
                        "BODY[HEADER.FIELDS (FROM SUBJECT)] {4}\r\nab\r\n "
                        "ENVELOPE (\"Mon, 1 Jan\" \"a \\\"quoted\\\" \\\\ subject\" NIL) "
                        "X-GM-MSGID 1278455344230334865 MODSEQ (12345678901234567890))\r\n"));
    EXPECT_EQ(p.parsed.kind, IMAP_RESPONSE_UNTAGGED);
    EXPECT_EQ(p.parsed.status, nullptr);
    EXPECT_EQ(dump(p.parsed.data),
              "12 FETCH (UID 48213 FLAGS (\\Seen $Junk) BODY[HEADER.FIELDS (FROM SUBJECT)] #0 "
              "ENVELOPE (\"Mon, 1 Jan\" \"a \"quoted\" \\ subject\" NIL) "
              "X-GM-MSGID 1278455344230334865 MODSEQ (12345678901234567890))");
    EXPECT_EQ(p.literal(0), "ab\r\n");
    // 20 digits overflow a number: the token stays an atom
    const ImapNode* modseq = p.parsed.data->next->next->first;
    while (modseq && !(modseq->kind == IMAP_NODE_ATOM && strcmp(modseq->text, "MODSEQ") == 0)) modseq = modseq->next;
    ASSERT_NE(modseq, nullptr);
    EXPECT_EQ(modseq->next->first->kind, IMAP_NODE_ATOM);
}

TEST(MailImapParse, SetsAndNumbers) {
    Parsed p;
    ASSERT_TRUE(p.parse("* SEARCH 2 84 882\r\n"));
    EXPECT_EQ(dump(p.parsed.data), "SEARCH 2 84 882");
    EXPECT_EQ(p.parsed.data->next->kind, IMAP_NODE_NUMBER);
    Parsed q;
    ASSERT_TRUE(q.parse("* ESEARCH (TAG \"L5\") UID ALL 1:5,9\r\n"));
    EXPECT_EQ(dump(q.parsed.data), "ESEARCH (TAG \"L5\") UID ALL 1:5,9");
}

TEST(MailImapParse, ListResponse) {
    Parsed p;
    ASSERT_TRUE(p.parse("* LIST (\\HasNoChildren \\Sent) \"/\" \"[Gmail]/Sent Mail\"\r\n"));
    EXPECT_EQ(dump(p.parsed.data), "LIST (\\HasNoChildren \\Sent) \"/\" \"[Gmail]/Sent Mail\"");
}

TEST(MailImapParse, UntaggedStatusWithCode) {
    Parsed p;
    ASSERT_TRUE(p.parse("* OK [PERMANENTFLAGS (\\Answered \\Seen \\*)] Limited\r\n"));
    EXPECT_STREQ(p.parsed.status, "OK");
    EXPECT_EQ(dump(p.parsed.code), "PERMANENTFLAGS (\\Answered \\Seen \\*)");
    EXPECT_EQ(std::string(p.parsed.text, p.parsed.text_length), "Limited");
}

TEST(MailImapParse, TaggedNoWithCode) {
    Parsed p;
    ASSERT_TRUE(p.parse("L7 no [TRYCREATE] Mailbox doesn't exist\r\n"));
    EXPECT_EQ(p.parsed.kind, IMAP_RESPONSE_TAGGED);
    EXPECT_EQ(std::string(p.parsed.tag, p.parsed.tag_length), "L7");
    EXPECT_STREQ(p.parsed.status, "NO");
    EXPECT_EQ(dump(p.parsed.code), "TRYCREATE");
    EXPECT_EQ(std::string(p.parsed.text, p.parsed.text_length), "Mailbox doesn't exist");
}

TEST(MailImapParse, CopyUidCode) {
    Parsed p;
    ASSERT_TRUE(p.parse("L8 OK [COPYUID 38505 304,319:320 3956:3958] Done\r\n"));
    EXPECT_EQ(dump(p.parsed.code), "COPYUID 38505 304,319:320 3956:3958");
}

TEST(MailImapParse, Continuation) {
    Parsed p;
    ASSERT_TRUE(p.parse("+ idling\r\n"));
    EXPECT_EQ(p.parsed.kind, IMAP_RESPONSE_CONTINUATION);
    EXPECT_EQ(std::string(p.parsed.text, p.parsed.text_length), "idling");
    Parsed q;
    ASSERT_TRUE(q.parse("+\r\n"));
    EXPECT_EQ(q.parsed.text_length, 0u);
}

TEST(MailImapParse, MalformedFails) {
    Parsed p;
    imap_reader_feed(p.reader, "* 1 FETCH (UID 1\r\n", 18);
    ASSERT_EQ(imap_reader_next(p.reader, &p.response), 1);
    EXPECT_EQ(imap_parse(p.parser, &p.response, &p.parsed), -1);
    Parsed q;
    imap_reader_feed(q.reader, "* LIST () \"/\" \"open\r\n", 21);
    ASSERT_EQ(imap_reader_next(q.reader, &q.response), 1);
    EXPECT_EQ(imap_parse(q.parser, &q.response, &q.parsed), -1);
}

// ── commands ───────────────────────────────────────────────────────────

namespace {
std::string bytes_of(const ImapCommand& c) { return std::string(c.bytes->str, c.bytes->length); }
}

TEST(MailImapCommand, QuotedAndAtoms) {
    ImapCommand c;
    ASSERT_TRUE(imap_command_init(&c, 0, false));
    ASSERT_TRUE(imap_command_begin(&c, "L1", "SELECT", 6));
    ASSERT_TRUE(imap_command_string(&c, "My \"Box\" \\ 1", 12));
    ASSERT_TRUE(imap_command_end(&c));
    EXPECT_EQ(bytes_of(c), "L1 SELECT \"My \\\"Box\\\" \\\\ 1\"\r\n");
    EXPECT_EQ(c.wait_count, 0);
    imap_command_free(&c);
}

TEST(MailImapCommand, ListsNumbersAndNil) {
    ImapCommand c;
    ASSERT_TRUE(imap_command_init(&c, 0, false));
    ASSERT_TRUE(imap_command_begin(&c, "L2", "UID STORE 1:4", 13));
    ASSERT_TRUE(imap_command_atom(&c, "+FLAGS.SILENT", 13));
    imap_command_list_begin(&c);
    ASSERT_TRUE(imap_command_atom(&c, "\\Seen", 5));
    imap_command_number(&c, 42);
    imap_command_nil(&c);
    imap_command_list_begin(&c);
    ASSERT_TRUE(imap_command_list_end(&c));
    ASSERT_TRUE(imap_command_list_end(&c));
    ASSERT_TRUE(imap_command_end(&c));
    EXPECT_EQ(bytes_of(c), "L2 UID STORE 1:4 +FLAGS.SILENT (\\Seen 42 NIL ())\r\n");
    imap_command_free(&c);
}

TEST(MailImapCommand, SynchronizingLiteralWaits) {
    ImapCommand c;
    ASSERT_TRUE(imap_command_init(&c, 0, false));
    ASSERT_TRUE(imap_command_begin(&c, "L3", "APPEND Drafts", 13));
    ASSERT_TRUE(imap_command_literal(&c, "Subject: x\r\n\r\nhi\r\n", 18));
    ASSERT_TRUE(imap_command_end(&c));
    std::string wire = bytes_of(c);
    EXPECT_EQ(wire, "L3 APPEND Drafts {18}\r\nSubject: x\r\n\r\nhi\r\n\r\n");
    ASSERT_EQ(c.wait_count, 1);
    EXPECT_EQ(wire.substr(0, c.waits[0]), "L3 APPEND Drafts {18}\r\n");
    imap_command_free(&c);
}

TEST(MailImapCommand, NonSynchronizingLiterals) {
    ImapCommand c;
    ASSERT_TRUE(imap_command_init(&c, 4096, false));   // LITERAL-
    ASSERT_TRUE(imap_command_begin(&c, "L4", "SEARCH CHARSET UTF-8 TEXT", 25));
    ASSERT_TRUE(imap_command_string(&c, "na\xc3\xafve", 6));   // 8-bit without UTF8=ACCEPT
    ASSERT_TRUE(imap_command_end(&c));
    EXPECT_EQ(bytes_of(c), "L4 SEARCH CHARSET UTF-8 TEXT {6+}\r\nna\xc3\xafve\r\n");
    EXPECT_EQ(c.wait_count, 0);
    imap_command_free(&c);
}

TEST(MailImapCommand, Utf8AcceptQuotes) {
    ImapCommand c;
    ASSERT_TRUE(imap_command_init(&c, 0, true));
    ASSERT_TRUE(imap_command_begin(&c, "L5", "SELECT", 6));
    ASSERT_TRUE(imap_command_string(&c, "Entw\xc3\xbcrfe", 9));
    ASSERT_TRUE(imap_command_end(&c));
    EXPECT_EQ(bytes_of(c), "L5 SELECT \"Entw\xc3\xbcrfe\"\r\n");
    imap_command_free(&c);
}

TEST(MailImapCommand, RejectsInjection) {
    ImapCommand c;
    ASSERT_TRUE(imap_command_init(&c, 0, false));
    EXPECT_FALSE(imap_command_begin(&c, "L6", "NOOP\r\nL7 DELETE INBOX", 21));
    imap_command_free(&c);
    ASSERT_TRUE(imap_command_init(&c, 0, false));
    ASSERT_TRUE(imap_command_begin(&c, "L6", "SELECT", 6));
    EXPECT_FALSE(imap_command_atom(&c, "IN BOX", 6));
    EXPECT_FALSE(imap_command_atom(&c, "a(b", 3));
    EXPECT_FALSE(imap_command_literal(&c, "a\0b", 3));
    // a string with a line break is never quoted: it becomes a literal
    ASSERT_TRUE(imap_command_string(&c, "x\r\nL7 DELETE INBOX", 18));
    EXPECT_NE(bytes_of(c).find("{18}\r\n"), std::string::npos);
    EXPECT_FALSE(imap_command_list_end(&c));
    imap_command_free(&c);
}
