// Readability.js oracle for lambda.doc.readability (vibe/Lambda_Pkg_Readability.md §7).
// For every <pages-dir>/<page>/source.html it writes expected.html and
// expected-metadata.json the way Mozilla's test harness runs Readability.js:
// jsdom at http://fakehost/test/page.html, comment nodes removed, and
// classesToPreserve ["caption"]; `readerable` comes from the unmodified page.
//
//   node utils/generate_readability_expected.js [pages-dir] [readability-dir]
//
// pages-dir defaults to test/lambda/readability/pages; readability-dir to the
// git-ignored checkout ref/readability/source (with its node_modules installed).
const fs = require("fs");
const path = require("path");

const root = path.resolve(__dirname, "..");
const pagesDir = path.resolve(process.argv[2] || path.join(root, "test/lambda/readability/pages"));
const readabilityDir = path.resolve(process.argv[3] || path.join(root, "ref/readability/source"));
const { JSDOM } = require(path.join(readabilityDir, "node_modules/jsdom"));
const { Readability, isProbablyReaderable } = require(readabilityDir);
const URL = "http://fakehost/test/page.html";

function removeComments(node) {
  for (let i = node.childNodes.length - 1; i >= 0; i--) {
    const child = node.childNodes[i];
    if (child.nodeType === child.COMMENT_NODE) node.removeChild(child);
    else if (child.nodeType === child.ELEMENT_NODE) removeComments(child);
  }
}

for (const page of fs.readdirSync(pagesDir).sort()) {
  const dir = path.join(pagesDir, page);
  const sourcePath = path.join(dir, "source.html");
  if (!fs.existsSync(sourcePath)) continue;
  const source = fs.readFileSync(sourcePath, "utf8").trim();
  const doc = new JSDOM(source, { url: URL }).window.document;
  removeComments(doc);
  const article = new Readability(doc, { classesToPreserve: ["caption"] }).parse();
  const readerable = isProbablyReaderable(new JSDOM(source, { url: URL }).window.document);
  const metadata = article ? {
    title: article.title, byline: article.byline, dir: article.dir, lang: article.lang,
    excerpt: article.excerpt, siteName: article.siteName, publishedTime: article.publishedTime, readerable,
  } : { readerable };
  fs.writeFileSync(path.join(dir, "expected.html"), article ? article.content : "");
  fs.writeFileSync(path.join(dir, "expected-metadata.json"), JSON.stringify(metadata, null, 2));
  console.log(`${page}: ${article ? "article" : "no article"}, readerable ${readerable}`);
}
