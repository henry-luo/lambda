// Markdown formatter block layer (format-md.cpp): each source is parsed and
// written back as CommonMark; `stable` checks that writing the result again
// changes nothing, so the output parses back to the same document.
fn md(src) => format(parse(src, 'markdown'), 'markdown') or "(error)"
fn stable(src) => md(md(src)) == md(src)

let cases = [
  // blocks and inline spans
  "# Title\n\nA paragraph with *em*, **strong**, `code`, and ~~gone~~.\n",
  "Setext\n======\n",
  "***\n",
  // tight, nested, loose, ordered, and task lists
  "- one\n- two\n  - nested\n- three\n",
  "1. first\n2. second\n\n   more of second\n3. third\n",
  "7) seven\n8) eight\n",
  "- [x] done\n- [ ] todo\n",
  "- a\n- b\n\n* c\n",
  // quotes and code
  "> quoted *text*\n>\n> - item in quote\n",
  "```js\nlet a = 1\n```\n",
  "````\n```inner```\n````\n",
  "Use `` a`b `` here.\n",
  // tables, breaks, links
  "| a | b |\n|:--|--:|\n| 1 | x\\|y |\n",
  "line one  \nline two\n",
  "[link](http://x.com \"T\") and ![img](a.png \"pic\") and <http://auto.link>\n",
  "[spaced](<a b.md>)\n",
  // text that would read as syntax is escaped where it would
  "\\# not a heading\n",
  "1\\. not a list\n",
  "\\- not a bullet\n",
  "\\> not a quote\n",
  "a\n\\===\n",
  "Mid-line 1. and # stay, but a\\*b and \\[x\\] escape.\n",
  "a \\<b> c & d \\&amp;\n",
  // extensions: superscript, emoji; raw HTML and math pass through
  "a^sup^ and :smile:\n",
  "<div>raw block</div>\n\nafter\n",
  "$$\nx^2\n$$\n",
  "$$\n\\begin{aligned} a &= b \\\\\n c &= d \\end{aligned}\n$$\n"
]
for (c in cases) [md(c), stable(c)];

// subscript and underline have no Markdown spelling: subscript is written as
// inline HTML, underline as its text
md("x") == "x\n";
format(<doc <p "H" <sub "2"> "O and " <u "under">>>, 'markdown') or "(error)"
