# Proposal: Local Semantic Search Tool for Markdown, Emails, Web Pages, and PDFs

## 1. Objective

Develop a **local-first semantic search tool** that can index and search across a collection of:

```text
Markdown documents
Emails
Saved web pages / HTML pages
PDF files
```

The tool will allow a user to enter a **phrase, sentence, or natural-language query**, then return the most relevant documents, files, and passages based on semantic meaning rather than only exact keyword matching.

Example queries:

```text
“emails discussing the project delay”
“notes about Lambda Jube sandbox design”
“documents mentioning tax filing deadlines”
“PDFs related to symbolic execution for C code”
“web pages about markdown publishing”
```

The system should use **open-source libraries and local models** as much as possible, with implementation preferably in **Node.js or Python**.

---

## 2. Problem Statement

Traditional file search usually depends on exact keyword matching. This works poorly when the user does not remember the exact wording used in the documents.

For example, a user might search:

```text
“delayed launch”
```

But the actual document may say:

```text
“we decided to postpone the release”
```

A semantic search system can understand that these are related and retrieve the correct document.

The proposed system will provide:

```text
semantic search
keyword search
metadata filtering
document preview
source references
local/private indexing
```

---

## 3. Proposed Solution

The tool will build a local searchable knowledge base from user documents.

High-level architecture:

```text
Documents
  ↓
Document parsers
  ↓
Text extraction
  ↓
Cleaning and normalization
  ↓
Chunking
  ↓
Embedding generation
  ↓
Vector index + metadata database
  ↓
Semantic search API
  ↓
User interface / CLI / local web app
```

The search process:

```text
User query
  ↓
Convert query into embedding
  ↓
Search vector database
  ↓
Optional keyword search
  ↓
Merge and rank results
  ↓
Return relevant files/passages
```

---

## 4. Supported Content Types

### 4.1 Markdown Documents

Markdown files will be parsed while preserving useful structure:

```text
file path
document title
headings
subheadings
paragraphs
code blocks
links
frontmatter metadata
last modified time
```

Recommended libraries:

Python:

```text
markdown-it-py
python-frontmatter
mistune
```

Node.js:

```text
remark
unified
gray-matter
markdown-it
```

---

### 4.2 Emails

Emails may come from:

```text
.eml files
mbox archives
IMAP mailbox export
Gmail Takeout
local mail folders
```

Extracted metadata:

```text
from
to
cc
bcc
subject
date
message id
thread id
body text
attachments
```

Recommended libraries:

Python:

```text
mailparser
mailbox
email built-in module
beautifulsoup4
```

Node.js:

```text
mailparser
imapflow
nodemailer/mailparser
```

Special handling should remove repeated quoted replies where possible, because email chains often contain duplicate content.

---

### 4.3 Web Pages

The system should support saved HTML files and optionally URLs.

Extracted metadata:

```text
page title
URL
main article text
headings
links
date if available
```

Recommended libraries:

Python:

```text
beautifulsoup4
readability-lxml
trafilatura
```

Node.js:

```text
cheerio
jsdom
mozilla-readability
```

For best search quality, the tool should extract the main article content instead of indexing menus, ads, navigation bars, and footers.

---

### 4.4 PDFs

PDFs should be parsed into text chunks.

Extracted metadata:

```text
file name
page number
document title if available
author if available
text per page
```

Recommended libraries:

Python:

```text
pymupdf
pdfplumber
pypdf
```

Node.js:

```text
pdf-parse
pdfjs-dist
```

For the first version, scanned image-only PDFs may be marked as unsupported or handled later with OCR.

Optional OCR support:

```text
Tesseract OCR
ocrmypdf
```

---

## 5. Indexing Design

### 5.1 Document Model

Each indexed document should have a document-level record:

```json
{
  "doc_id": "uuid",
  "source_type": "markdown | email | html | pdf",
  "title": "Document title",
  "path": "/docs/example.md",
  "created_at": "2026-05-03T10:00:00",
  "updated_at": "2026-05-03T10:00:00",
  "metadata": {
    "author": "...",
    "subject": "...",
    "from": "...",
    "url": "..."
  }
}
```

Each document is split into smaller searchable chunks:

```json
{
  "chunk_id": "uuid",
  "doc_id": "uuid",
  "chunk_index": 12,
  "text": "Relevant text passage...",
  "heading": "Design Notes",
  "page_number": 4,
  "embedding": [0.012, -0.032, ...],
  "metadata": {
    "source_type": "pdf",
    "path": "/papers/example.pdf"
  }
}
```

---

### 5.2 Chunking Strategy

The system should avoid blindly splitting every document into fixed-size chunks. Instead, it should use structure-aware chunking.

Recommended chunking rules:

For Markdown:

```text
split by heading and subheading
keep heading path with each chunk
split long sections into smaller chunks
```

For Emails:

```text
one email body as one chunk if short
split long emails by paragraph
preserve subject, sender, recipient, and date
```

For Web Pages:

```text
split by article sections
preserve page title and URL
```

For PDFs:

```text
split by page or paragraph
preserve page number
```

Suggested chunk size:

```text
300–800 tokens per chunk
```

Suggested overlap:

```text
50–100 tokens
```

---

## 6. Embedding Model

The system should use a local open-source embedding model.

Recommended models:

```text
BAAI/bge-small-en-v1.5
BAAI/bge-base-en-v1.5
sentence-transformers/all-MiniLM-L6-v2
nomic-ai/nomic-embed-text-v1.5
mixedbread-ai/mxbai-embed-large-v1
```

For a lightweight local setup:

```text
BAAI/bge-small-en-v1.5
```

For better quality:

```text
BAAI/bge-base-en-v1.5
```

For strong general-purpose local semantic search:

```text
nomic-ai/nomic-embed-text-v1.5
```

Python implementation options:

```text
sentence-transformers
FlagEmbedding
transformers
llama-cpp-python for GGUF embedding models
```

Node.js implementation options:

```text
@xenova/transformers
transformers.js
ollama local embedding API
```

Recommended first implementation:

```text
Python + sentence-transformers
```

This is the simplest and most mature path for local embeddings.

---

## 7. Vector Storage

The vector database stores chunk embeddings and supports nearest-neighbor search.

Recommended local options:

### Option A: SQLite + sqlite-vec

Best for simple local deployment.

Pros:

```text
single local database file
easy backup
simple deployment
good for personal or small-team use
```

Cons:

```text
less scalable than dedicated vector databases
```

---

### Option B: LanceDB

Good local vector database for Python and Node.js.

Pros:

```text
simple API
local-first
fast vector search
works well for document collections
supports metadata
```

Cons:

```text
newer ecosystem than SQLite/Postgres
```

---

### Option C: Qdrant Local

Good if the system may later become a server product.

Pros:

```text
strong vector search
metadata filtering
Docker-friendly
production-ready
```

Cons:

```text
requires running a service
more operational complexity
```

---

### Option D: Postgres + pgvector

Good if the user wants a serious backend.

Pros:

```text
standard relational database
good metadata filtering
full-text search support
pgvector support
production-friendly
```

Cons:

```text
heavier setup
requires PostgreSQL
```

---

## 8. Recommended Stack

### Simple Local Version

```text
Language: Python
Parsers: pymupdf, beautifulsoup4, markdown-it-py, mailparser
Embeddings: sentence-transformers
Vector DB: LanceDB or sqlite-vec
API: FastAPI
UI: simple local web UI
```

This is the recommended MVP stack.

---

### Node.js Version

```text
Language: Node.js / TypeScript
Parsers: remark, mailparser, cheerio, pdf-parse
Embeddings: transformers.js or Ollama embeddings
Vector DB: LanceDB or Qdrant
API: Express / Fastify
UI: React / Next.js
```

This is better if the project will eventually become a polished desktop or web app.

---

### Best Overall Recommendation

Use a hybrid approach:

```text
Python backend for ingestion and embeddings
Node.js / React frontend for UI
SQLite or LanceDB for local storage
```

This gives the best balance:

```text
Python = stronger document parsing and ML ecosystem
Node.js = better UI and desktop/web app ecosystem
```

---

## 9. Search Features

The search tool should support:

### Basic Semantic Search

User enters:

```text
“documents about postponing the release”
```

System returns:

```text
1. project-plan.md
2. email from Alice: “Release timeline”
3. launch-notes.pdf, page 4
```

---

### Search Result Display

Each result should show:

```text
title
source type
file path / email subject / URL
relevant text snippet
score
metadata
open-file link
```

Example:

```text
Result 1
Title: Lambda Jube Runtime Notes
Source: Markdown
Path: /docs/lambda-jube/runtime.md
Matched passage:
“Jube is designed as a lightweight sandbox runtime...”
```

---

### Metadata Filters

The tool should allow filtering by:

```text
source type
date range
file path
sender
recipient
email subject
PDF page
web domain
tags
```

Example:

```text
semantic query: “company strike off”
filter: source_type = email
date: after 2025-01-01
```

---

### Hybrid Search

Semantic search should be combined with keyword search.

This improves searches involving exact terms such as:

```text
invoice number
company name
ticket ID
file name
stock ticker
email address
code symbol
```

Recommended design:

```text
semantic vector score
+ keyword BM25 score
+ metadata relevance score
= final ranking score
```

Possible libraries:

Python:

```text
rank-bm25
tantivy
sqlite FTS5
Postgres full-text search
```

Node.js:

```text
MiniSearch
FlexSearch
Lunr.js
Elasticlunr
```

For local MVP:

```text
SQLite FTS5 + vector search
```

---

## 10. Optional Reranking

For better quality, add a local reranker.

Process:

```text
1. Retrieve top 50 chunks using semantic + keyword search
2. Rerank them using a cross-encoder model
3. Return top 10 best results
```

Open-source reranker options:

```text
BAAI/bge-reranker-base
cross-encoder/ms-marco-MiniLM-L-6-v2
jinaai/jina-reranker-v1-tiny-en
```

Recommended for MVP:

```text
skip reranking first
add reranker in version 2
```

Recommended for high-quality version:

```text
bge-reranker-base
```

---

## 11. Local-First and Privacy

The system should be designed to run locally.

Privacy requirements:

```text
documents remain on the user’s machine
embeddings are generated locally
no document text is sent to cloud APIs by default
local database storage
optional encrypted index storage
```

Optional future setting:

```text
allow cloud model provider only if user explicitly enables it
```

---

## 12. User Interface

### MVP UI

A local browser interface:

```text
search bar
filters panel
result list
document preview pane
open original file button
```

Possible UI stack:

```text
React + Vite
Next.js
Electron
Tauri
```

For a lightweight local app:

```text
FastAPI backend + React frontend
```

---

### CLI Version

A CLI can be built first for faster development:

```bash
semsearch index ./docs
semsearch search "notes about C pointer safety"
semsearch search "emails about Krabi booking" --type email
```

Example output:

```text
1. docs/lambda/type-system.md
   Score: 0.87
   “Tagged unions can be used to encode safe casts...”

2. emails/project-update.eml
   Score: 0.82
   “We discussed delaying the launch until...”
```

---

## 13. API Design

Example API endpoints:

```http
POST /index
POST /search
GET /documents/:doc_id
GET /chunks/:chunk_id
DELETE /documents/:doc_id
POST /reindex
```

Search request:

```json
{
  "query": "documents discussing release delay",
  "top_k": 10,
  "filters": {
    "source_type": ["markdown", "email"],
    "date_after": "2025-01-01"
  }
}
```

Search response:

```json
{
  "results": [
    {
      "doc_id": "doc_123",
      "chunk_id": "chunk_456",
      "title": "Release Planning Notes",
      "source_type": "markdown",
      "path": "/docs/release.md",
      "snippet": "The release was postponed because...",
      "score": 0.91,
      "metadata": {
        "heading": "Timeline",
        "modified_at": "2026-04-01"
      }
    }
  ]
}
```

---

## 14. Development Phases

### Phase 1: MVP Semantic Search

Deliverables:

```text
Markdown parser
PDF text parser
basic email parser
local embedding generation
local vector database
CLI search
basic result ranking
```

Goal:

```text
User can index a folder and search semantically.
```

---

### Phase 2: Web UI and Metadata Filters

Deliverables:

```text
local web UI
source type filters
date filters
file preview
open original file
incremental reindexing
```

Goal:

```text
User can interactively search and browse results.
```

---

### Phase 3: Hybrid Search

Deliverables:

```text
keyword index
BM25/FTS search
hybrid ranking
exact-match boosting
better snippets
```

Goal:

```text
Improve search quality for names, IDs, technical terms, and exact phrases.
```

---

### Phase 4: Reranking and Quality Improvements

Deliverables:

```text
local reranker model
query expansion
duplicate detection
email thread cleanup
PDF page-level references
```

Goal:

```text
Return more accurate top results.
```

---

### Phase 5: Optional RAG Answering

This proposal focuses on semantic document search, but the system can later add RAG.

Possible feature:

```text
User asks a question
system retrieves relevant chunks
LLM summarizes answer with citations
```

Example:

```text
Question:
“What did we decide about the launch date?”

Answer:
“The launch was postponed to May because testing was incomplete. The decision appears in release-notes.md and Alice’s email dated...”
```

Local LLM options:

```text
Llama 3.x
Mistral
Qwen
Gemma
Phi
```

Runtime options:

```text
Ollama
llama.cpp
LM Studio
vLLM
```

---

## 15. Risks and Mitigations

### Risk 1: Poor PDF Extraction

Some PDFs have bad text extraction or scanned images.

Mitigation:

```text
support OCR later
store page number
allow manual reindexing
show extraction warnings
```

---

### Risk 2: Duplicate Email Content

Email threads often repeat previous replies.

Mitigation:

```text
remove quoted text
deduplicate similar chunks
preserve thread metadata
```

---

### Risk 3: Bad Chunking

Poor chunking can reduce search quality.

Mitigation:

```text
use structure-aware chunking
preserve headings
tune chunk size
inspect search results during testing
```

---

### Risk 4: Local Model Performance

Local embedding models can be slow on large collections.

Mitigation:

```text
batch embeddings
cache embeddings
incremental indexing
use smaller model for MVP
support background indexing
```

---

## 16. Recommended MVP Technology Choice

For the first working version, use:

```text
Python
FastAPI
sentence-transformers
BAAI/bge-small-en-v1.5
LanceDB
PyMuPDF
markdown-it-py
mailparser
beautifulsoup4
```

Why this stack:

```text
fast to build
fully local
good open-source ecosystem
works across Markdown, emails, HTML, and PDFs
easy to later add web UI
```

Optional frontend:

```text
React + Vite
```

Optional desktop wrapper:

```text
Tauri
```

---

## 17. Expected Outcome

The final tool will allow users to search large personal or project document collections using natural language.

The user will be able to ask:

```text
“Where did I write about state machine support?”
“Find emails discussing the budget issue.”
“Show PDFs related to symbolic execution.”
“Find web pages about Markdown publishing.”
```

And receive ranked results with:

```text
document title
file path or source
matched passage
metadata
relevance score
link to original file
```

---

## 18. Summary

The proposed tool should be a **local-first semantic search system** over Markdown, emails, web pages, and PDFs.

The best implementation strategy is:

```text
Phase 1:
Python semantic search engine

Phase 2:
local web UI

Phase 3:
hybrid semantic + keyword search

Phase 4:
reranking and smarter retrieval

Phase 5:
optional RAG-based Q&A
```

Recommended MVP stack:

```text
Python + FastAPI
sentence-transformers
BGE embedding model
LanceDB
PyMuPDF
markdown-it-py
mailparser
BeautifulSoup
```

Recommended long-term architecture:

```text
Hybrid search
+ metadata filtering
+ reranking
+ optional local RAG
```

This gives a practical, private, open-source system that can grow from a simple semantic file searcher into a full local knowledge assistant.
