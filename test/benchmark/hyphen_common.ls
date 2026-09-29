// shared workload inputs and exact outputs for both hyphen ports.
pub let hyphen_cases = [
    ["A certain king had a beautiful garden, and every morning he walked through it to admire the flowers.",
        "A cer-tain king had a beau-ti-ful gar-den, and every morn-ing he walked through it to ad-mire the flow-ers."],
    ["The tortoise never stopped for a moment, walking slowly but steadily right to the end of the course.",
        "The tor-toise nev-er stopped for a mo-ment, walk-ing slow-ly but steadi-ly right to the end of the course."],
    ["A compiler transforms structured source text into executable instructions while preserving useful diagnostics.",
        "A com-pil-er trans-forms struc-tured source text into ex-e-cutable in-struc-tions while pre-serv-ing use-ful di-ag-nos-tics."],
    ["Text processing includes punctuation, capitalization, multiline paragraphs, and carefully selected exceptions.",
        "Text pro-cess-ing in-cludes punc-tu-a-tion, cap-i-tal-iza-tion, mul-ti-line para-graphs, and care-ful-ly se-lect-ed ex-cep-tions."],
    ["<article><h1>Hyphenation benchmark</h1><p>Beautiful documents require readable typography and consistent line breaking.</p></article>",
        "<article><h1>Hy-phen-ation bench-mark</h1><p>Beau-ti-ful doc-u-ments re-quire read-able ty-pog-ra-phy and con-sis-tent line break-ing.</p></article>"],
    ["The algorithm combines a pattern trie with exception handling and a configurable hyphenation character.",
        "The al-go-rithm com-bines a pat-tern trie with ex-cep-tion han-dling and a con-fig-urable hy-phen-ation char-ac-ter."],
    ["associate associates declination obligatory philanthropic",
        "as-soc-iate as-soc-iates dec-lin-ati-on oblig-at-ory phil-ant-hropic"],
    ["recognizance reformation retribution reciprocity table present projects",
        "re-cogn-iza-nce ref-orm-ati-on ret-rib-uti-on reci-procity ta-ble present projects"],
    ["<article data-note=\"associate\">Hyphenation &amp; typography</article>",
        "<article data-note=\"associate\">Hy-phen-ation &amp; ty-pog-ra-phy</article>"],
    ["co-operate already-hyphenated exceptionally punctuation's boundary",
        "co-operate already-hyphenated ex-cep-tion-al-ly punc-tu-a-tion's bound-ary"],
    ["supercalifragilisticexpialidocious internationalization configuration",
        "su-per-cal-ifrag-ilis-tic-ex-pi-ali-do-cious in-ter-na-tion-al-iza-tion con-fig-u-ra-tion"],
    ["Configuration configuration configuration.",
        "Con-fig-u-ra-tion con-fig-u-ra-tion con-fig-u-ra-tion."],
    ["<3 is not markup, while <em>configuration</em> remains a word.",
        "<3 is not markup, while <em>con-fig-u-ra-tion</em> re-mains a word."]
]

pub pn is_ascii_letter(ch: string) bool {
    let cp = ord(ch)
    return (cp >= 65 and cp <= 90) or (cp >= 97 and cp <= 122)
}

pub pn is_word_char(ch: string) bool {
    return is_ascii_letter(ch) or ch == "'"
}

pub pn starts_html_tag(text: string, index: int) bool {
    return text[index] == "<" and index + 1 < len(text) and
        (is_ascii_letter(text[index + 1]) or text[index + 1] == "/")
}

