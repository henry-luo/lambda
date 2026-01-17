# LaTeXML Architecture and Design Analysis

This document analyzes the LaTeXML codebase to understand its architecture, design patterns, and implementation strategies for converting LaTeX to XML.

## 1. Overview

**LaTeXML** is a sophisticated LaTeX-to-XML converter developed by NIST (National Institute of Standards and Technology), primarily for the Digital Library of Mathematical Functions (DLMF). It faithfully emulates TeX's behavior while producing semantic XML output, including high-quality MathML.

### Design Goals

1. **Faithful emulation of TeX's behavior** - Mimics TeX's tokenization, expansion, and digestion phases
2. **Easily extensible** - Package binding system allows new packages to be supported
3. **Lossless conversion** - Preserves both semantic and presentation cues
4. **Abstract document type** - Uses an extensible, LaTeX-like XML schema
5. **Mathematical semantics** - Infers structure of mathematical content for MathML generation

---

## 2. Pipeline Architecture

LaTeXML's conversion pipeline closely mirrors TeX's own "digestive tract" metaphor. The system consists of two main programs:

```
┌─────────────────────────────────────────────────────────────────┐
│                        latexml                                   │
│  ┌──────────┐   ┌────────┐   ┌─────────┐   ┌──────────────────┐ │
│  │  Mouth   │ → │ Gullet │ → │ Stomach │ → │     Document     │ │
│  │ (Tokens) │   │(Expand)│   │(Digest) │   │  (Construction)  │ │
│  └──────────┘   └────────┘   └─────────┘   └──────────────────┘ │
│                                                    ↓            │
│                              ┌────────────────────────────────┐ │
│                              │ Rewriting → Math Parsing → XML │ │
│                              └────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
                                    ↓
┌─────────────────────────────────────────────────────────────────┐
│                      latexmlpost                                 │
│  Split → Scan → Index → Bibliography → CrossRef → Math/Graphics │
│                           → XSLT → Writer                        │
└─────────────────────────────────────────────────────────────────┘
```

### 2.1 Core Processing Stages (latexml)

#### Stage 1: Tokenization (Mouth)
**File:** `lib/LaTeXML/Core/Mouth.pm`

The Mouth converts character input into Tokens according to TeX's catcode system.

```perl
# Token representation: [string, catcode]
# Catcode constants from Core/Token.pm
use constant CC_ESCAPE  => 0;   # \
use constant CC_BEGIN   => 1;   # {
use constant CC_END     => 2;   # }
use constant CC_MATH    => 3;   # $
use constant CC_LETTER  => 11;  # a-z, A-Z
use constant CC_OTHER   => 12;  # other characters
use constant CC_CS      => 16;  # control sequence (LaTeXML extension)
```

**Token Class** (`Core/Token.pm`):
- Tokens are `[string, catcode]` pairs
- Provides constructors: `T_CS('\command')`, `T_BEGIN`, `T_END`, `T_MATH`, etc.
- `Tokens` class wraps lists of tokens
- `Tokenize($string)` and `TokenizeInternal($string)` for conversion

#### Stage 2: Expansion (Gullet)
**File:** `lib/LaTeXML/Core/Gullet.pm`

The Gullet performs macro expansion in a **pull mode**:
- Reads tokens from the Mouth
- Expands macros (Expandables) 
- Parses tokens into core datatypes (Number, Dimension, etc.)
- Does NOT execute primitives or constructors

```perl
# Gullet maintains a stack of Mouths
sub openMouth {
    my ($self, $mouth, $noautoclose) = @_;
    unshift(@{ $$self{mouthstack} }, [$$self{mouth}, $$self{pushback}]);
    $$self{mouth} = $mouth;
}
```

#### Stage 3: Digestion (Stomach)
**File:** `lib/LaTeXML/Core/Stomach.pm`

The Stomach digests expanded tokens into **Boxes** and **Whatsits**:

```perl
# Digestion converts:
# - Textual tokens → Boxes (with font information)
# - Primitives → side effects + possibly Boxes
# - Constructors → Whatsits (carry construction instructions)
```

**Key Classes:**
- `Box` - Digested text content with font
- `List` - Collection of Boxes
- `Whatsit` - Digested constructor with arguments and properties

#### Stage 4: Document Construction
**File:** `lib/LaTeXML/Core/Document.pm`

Converts the digested material (Boxes/Whatsits) into XML DOM:

```perl
# Document construction:
# - Boxes → text nodes with font handling
# - Whatsits → XML fragments via Constructor patterns
# 
# Uses Model to validate and auto-open/close elements
sub absorb {
    # Absorbing a Box → text content
    # Absorbing a Whatsit → invokes constructor's replacement
}
```

#### Stage 5: Rewriting
**File:** `lib/LaTeXML/Core/Rewrite.pm`

Post-construction transformations:
- Ligature handling
- Math token combining
- Document tree modifications

#### Stage 6: Math Parsing  
**File:** `lib/LaTeXML/MathParser.pm`

Grammar-based parser using `Parse::RecDescent`:
- Parses tokenized mathematics
- Infers structure of expressions
- Generates structured representation for MathML conversion

---

## 3. AST / Intermediate Representation

LaTeXML doesn't use a traditional single AST. Instead, it has **multiple intermediate representations** at each stage:

### 3.1 Token Stream

```perl
# Tokens are the first IR - [string, catcode] pairs
my $token = bless ['\section', CC_CS], 'LaTeXML::Core::Token';
my $tokens = Tokens(T_CS('\section'), T_BEGIN, ...);
```

### 3.2 Digested Forms (Box, List, Whatsit)

**Box** - Simple digested content:
```perl
# lib/LaTeXML/Core/Box.pm
{ string => "Hello",
  tokens => $original_tokens,
  properties => { font => $font, locator => $loc, mode => 'text' }
}
```

**Whatsit** - Digested constructor invocation:
```perl
# lib/LaTeXML/Core/Whatsit.pm
{ definition => $constructor_definition,
  args => [@digested_arguments],
  properties => { font => ..., locator => ..., body => $list, ... }
}
```

### 3.3 XML DOM (Document)

The final representation is an `XML::LibXML::Document` with LaTeXML's custom schema:
- Elements like `ltx:section`, `ltx:equation`, `ltx:XMath`
- Math represented with `ltx:XMath`, `ltx:XMApp`, `ltx:XMTok`

### 3.4 Math Representation (XMath)

LaTeXML uses an intermediate math representation before MathML:

```xml
<!-- XMath internal representation -->
<ltx:XMath>
  <ltx:XMApp>
    <ltx:XMTok meaning="divide" role="FRACOP"/>
    <ltx:XMArg><ltx:XMTok>a</ltx:XMTok></ltx:XMArg>
    <ltx:XMArg><ltx:XMTok>b</ltx:XMTok></ltx:XMArg>
  </ltx:XMApp>
</ltx:XMath>
```

Key math elements:
- `XMath` - Root math container
- `XMTok` - Math token (with meaning, role attributes)
- `XMApp` - Function application
- `XMArg` - Argument wrapper
- `XMDual` - Dual content/presentation representation
- `XMRef` - Reference to shared subexpression

---

## 4. Flexible Package Binding System

LaTeXML's binding system is one of its most powerful features, allowing LaTeX packages to be reimplemented for semantic XML output.

### 4.1 Binding File Structure

Binding files have extension `.ltxml` and are Perl modules:

```perl
# lib/LaTeXML/Package/mypackage.sty.ltxml
package LaTeXML::Package::Pool;
use strict;
use warnings;
use LaTeXML::Package;

# Package options
DeclareOption('option1', sub { ... });
ProcessOptions();

# Definitions
DefMacro('\mymacro{}', '\textbf{#1}');
DefConstructor('\mysection{}', '<ltx:section>#1</ltx:section>');

1;  # Required for Perl module
```

### 4.2 Definition Types

LaTeXML provides several definition forms, each for different purposes:

#### DefMacro - Expandable definitions
```perl
# Simple text replacement (like \newcommand)
DefMacro('\mybold{}', '\textbf{#1}');

# With Perl code
DefMacro('\today', sub {
    my ($gullet) = @_;
    return Tokens(Explode(localtime));
});
```

#### DefPrimitive - Side-effect commands
```perl
# Execute code during digestion
DefPrimitive('\begingroup', sub { $_[0]->begingroup; });
DefPrimitive('\setcounter{}{}', sub { 
    my ($stomach, $name, $value) = @_;
    # Side effect: modify counter
});
```

#### DefConstructor - XML generation
```perl
# String pattern - most common form
DefConstructor('\section{}', 
    '<ltx:section><ltx:title>#1</ltx:title></ltx:section>');

# With options and properties
DefConstructor('\href{url}{}',
    '<ltx:ref href="#1">#2</ltx:ref>',
    properties => sub {
        my ($stomach, $url, $text) = @_;
        return (href => CleanURL($url));
    });

# With before/after daemons
DefConstructor('\footnote{}',
    '<ltx:note class="footnote" mark="#refnum">#1</ltx:note>',
    beforeDigest => sub { ... },
    afterDigest => sub { 
        my ($stomach, $whatsit) = @_;
        $whatsit->setProperty('refnum', ...);
    });
```

#### DefEnvironment - Environment handling
```perl
DefEnvironment('{quote}',
    '<ltx:quote>#body</ltx:quote>');

DefEnvironment('{enumerate}',
    '<ltx:enumerate>#body</ltx:enumerate>',
    afterDigestBegin => sub { ... },
    beforeDigestEnd => sub { ... });
```

#### DefMath - Mathematical constructs
```perl
DefMath('\sin', 'sin', role => 'TRIGFUNCTION');
DefMath('\frac{}{}', 
    '<ltx:XMApp><ltx:XMTok meaning="divide"/><ltx:XMArg>#1</ltx:XMArg><ltx:XMArg>#2</ltx:XMArg></ltx:XMApp>',
    reversion => '\frac{#1}{#2}');
```

### 4.3 Parameter Specifications

LaTeXML extends TeX's parameter syntax:

| Pattern | Description |
|---------|-------------|
| `{}` | Required braced argument |
| `[]` | Optional bracketed argument |
| `[Default:X]` | Optional with default value X |
| `{Type}` | Argument of specific type |
| `OptionalKeyVals:Name` | Key-value pairs |
| `Semiverbatim` | Nearly verbatim text |

```perl
# Complex parameter example
DefConstructor('\includegraphics OptionalKeyVals:Gin Semiverbatim',
    '<ltx:graphics graphic="#2"/>');
```

### 4.4 Package Loading Mechanism

```perl
# Loading packages
RequirePackage('graphicx');           # Load graphicx.sty.ltxml
LoadClass('article');                 # Load article.cls.ltxml

# With options
RequirePackage('geometry', options => [qw(margin=1in)]);

# Option declaration
DeclareOption('draft', sub { AssignValue('DRAFT', 1); });
DeclareOption('*', sub { ... });  # Default handler

ProcessOptions();
ExecuteOptions('draft');
```

### 4.5 File Search Order

When loading a package `foo.sty`:
1. Look for `foo.sty.ltxml` (LaTeXML binding)
2. If `--includestyles`, try `foo.sty` (raw TeX)
3. Search in: current directory, `--path` directories, installation directories

---

## 5. State Management

### 5.1 State Architecture
**File:** `lib/LaTeXML/Core/State.pm`

LaTeXML's State implements TeX's scoping with efficient data structures:

```perl
# State maintains multiple tables:
{
    value => {},      # General values (registers, etc.)
    meaning => {},    # Control sequence definitions
    catcode => {},    # Character catcodes
    mathcode => {},   # Math character codes
    undo => [{...}],  # Stack frames for scoping
}

# Each table entry is a stack:
# $$self{value}{name} = [$current_value, $prev_value, ...]
```

### 5.2 Scoping Implementation

```perl
# Push frame (begin group)
sub pushFrame {
    unshift(@{ $$self{undo} }, {});
}

# Pop frame (end group) - undo all bindings
sub popFrame {
    my $frame = shift(@{ $$self{undo} });
    foreach my $table (keys %$frame) {
        foreach my $key (keys %{$frame->{$table}}) {
            my $n = $frame->{$table}{$key};
            splice(@{ $$self{$table}{$key} }, 0, $n);  # Remove n values
        }
    }
}

# Assignment with scope
sub assign_internal {
    my ($self, $table, $key, $value, $scope) = @_;
    if ($scope eq 'global') {
        # Remove all local bindings, set globally
    } elsif ($scope eq 'local') {
        # Push onto current frame
        $$self{undo}[0]{$table}{$key}++;
        unshift(@{ $$self{$table}{$key} }, $value);
    }
}
```

### 5.3 Named Scopes

LaTeXML extends TeX with named scopes for deferred activation:

```perl
# Stash definitions under a name
AssignValue('myvalue', $value, 'myscope');

# Later activate the scope
activateScope('myscope');  # Makes stashed values take effect
```

---

## 6. Document Model

### 6.1 Schema System
**File:** `lib/LaTeXML/Common/Model.pm`

LaTeXML supports both RelaxNG and DTD schemas:

```perl
# Declaring schema
RelaxNGSchema('LaTeXML');

# Registering namespaces
RegisterNamespace('ltx', 'http://dlmf.nist.gov/LaTeXML');
RegisterNamespace('m', 'http://www.w3.org/1998/Math/MathML');
```

### 6.2 Auto-open/close Elements

The Model enables SGML-like flexibility:

```perl
# Tag properties
Tag('ltx:subsection', 
    autoClose => 1,    # Can be auto-closed
    autoOpen => 1);    # Can be auto-opened

# When inserting <section>, auto-close open <subsection>
# When inserting <item>, auto-open <itemize> if needed
```

### 6.3 Document Construction Hooks

```perl
Tag('ltx:section',
    afterOpen => sub {
        my ($document, $node, $box) = @_;
        # Add attributes, modify node
    },
    afterClose => sub {
        my ($document, $node, $box) = @_;
        # Post-processing
    });
```

### 6.4 XML DOM Schema (RelaxNG)

LaTeXML defines its XML schema using **RelaxNG Compact Syntax** in `lib/LaTeXML/resources/RelaxNG/`.

#### Schema Organization

The schema is modular with a main entry point:

```
LaTeXML.rnc              # Main schema - includes all modules
├── LaTeXML-common.rnc   # Common definitions, namespaces
├── LaTeXML-structure.rnc # Document structure (section, chapter, etc.)
├── LaTeXML-block.rnc    # Block-level elements (para, figure, table)
├── LaTeXML-inline.rnc   # Inline elements (text, ref, cite)
├── LaTeXML-math.rnc     # Math elements (XMath, XMApp, XMTok)
├── LaTeXML-tabular.rnc  # Table structures
├── LaTeXML-picture.rnc  # Graphics and pictures
├── LaTeXML-bib.rnc      # Bibliography elements
└── LaTeXML-para.rnc     # Paragraph-level constructs
```

#### Namespace

```
Namespace URI: http://dlmf.nist.gov/LaTeXML
Default prefix: ltx
```

#### Key Element Categories

**Structure Elements** (LaTeXML-structure.rnc):
- `ltx:document` - Root document container
- `ltx:part`, `ltx:chapter`, `ltx:section`, `ltx:subsection`, etc.
- `ltx:title`, `ltx:creator`, `ltx:abstract`
- `ltx:appendix`, `ltx:bibliography`, `ltx:index`

**Block Elements** (LaTeXML-block.rnc):
- `ltx:para`, `ltx:p` - Paragraphs
- `ltx:figure`, `ltx:table`, `ltx:float`
- `ltx:equation`, `ltx:equationgroup`
- `ltx:itemize`, `ltx:enumerate`, `ltx:description`
- `ltx:verbatim`, `ltx:listing`

**Inline Elements** (LaTeXML-inline.rnc):
- `ltx:text` - Styled text with font attributes
- `ltx:ref`, `ltx:cite` - References and citations
- `ltx:note` - Footnotes and margin notes
- `ltx:anchor`, `ltx:break`

**Math Elements** (LaTeXML-math.rnc):
- `ltx:Math` - Top-level math container
- `ltx:XMath` - Internal math representation root
- `ltx:XMApp` - Function/operator application
- `ltx:XMTok` - Math token with `meaning`, `role`, `name` attributes
- `ltx:XMArg` - Argument wrapper
- `ltx:XMRef` - Reference to shared subexpression (via `idref`)
- `ltx:XMHint` - Spacing/rendering hints
- `ltx:XMWrap` - Grouping wrapper
- `ltx:XMDual` - Dual content/presentation representation
- `ltx:XMText` - Text within math
- `ltx:XMArray` - Matrix/array structure

#### Common Attributes

Most elements share these common attributes:

```rnc
Common.attributes =
  attribute xml:id { xsd:ID }?,        # Unique identifier
  attribute class { text }?,            # CSS-like classes
  attribute cssstyle { text }?,         # Inline CSS styles
  attribute fragid { text }?            # Fragment identifier
```

Math tokens (`XMTok`) have semantic attributes:

```rnc
XMTok.attributes =
  attribute name { text }?,             # Display name
  attribute meaning { text }?,          # Semantic meaning (e.g., "plus", "divide")
  attribute role { text }?,             # Grammatical role (ADDOP, MULOP, etc.)
  attribute scriptpos { text }?,        # Script positioning
  attribute possibleFunction { text }?  # Possible function indicator
```

### 6.5 Source Position Tracking

LaTeXML tracks source positions but stores them in a **side-table**, not in the serialized XML.

#### Locator Class
**File:** `lib/LaTeXML/Common/Locator.pm`

```perl
# Locator stores source position info
{
    source  => "filename.tex",    # Source file path
    fromLine => 42,               # Starting line number
    fromCol  => 10,               # Starting column
    toLine   => 42,               # Ending line number
    toCol    => 25                # Ending column
}
```

#### Storage Mechanism

Position info is stored in `Document`'s `node_boxes` hash, NOT as XML attributes:

```perl
# In Document.pm
sub setNodeBox {
    my ($self, $node, $box) = @_;
    return unless $box;
    my $id = $node->getAttribute('_box');  # Internal attribute
    if (!defined $id) {
        $id = '_box' . (++$$self{node_boxes_id});
        $node->setAttribute('_box', $id);  # Add tracking ID
    }
    $$self{node_boxes}{$id} = $box;        # Store in side-table
}

sub getNodeBox {
    my ($self, $node) = @_;
    if (my $id = $node->getAttribute('_box')) {
        return $$self{node_boxes}{$id};    # Retrieve from side-table
    }
    return;
}
```

#### Accessing Position Info

During processing, position info can be retrieved via the Box associated with each node:

```perl
my $box = $document->getNodeBox($node);
if ($box) {
    my $locator = $box->getLocator;
    my $source = $locator->getSource;      # "file.tex"
    my $line = $locator->getLineNumber;    # 42
}
```

**Note:** By default, the serialized XML output does NOT contain source positions. The position information is available during processing and post-processing but is not written to the final XML files.

---

## 7. Post-Processing Pipeline (latexmlpost)

### 7.1 Filter Chain
**File:** `lib/LaTeXML/Post.pm`

Post-processing applies a sequence of filter modules:

```perl
sub ProcessChain {
    my ($self, $doc, @postprocessors) = @_;
    my @docs = ($doc);
    foreach my $processor (@postprocessors) {
        my @newdocs = ();
        foreach my $doc (@docs) {
            if (my @nodes = $processor->toProcess($doc)) {
                push(@newdocs, $processor->process($doc, @nodes));
            }
        }
        @docs = @newdocs;
    }
    return @docs;
}
```

### 7.2 Available Post-processors

| Module | Purpose |
|--------|---------|
| `Split.pm` | Split document into pages |
| `Scan.pm` | Collect IDs, labels, references |
| `MakeIndex.pm` | Build index from `\index` commands |
| `MakeBibliography.pm` | Process bibliography |
| `CrossRef.pm` | Resolve cross-references |
| `MathML.pm` | Convert XMath to MathML |
| `MathImages.pm` | Render math to images |
| `Graphics.pm` | Process graphics |
| `SVG.pm` | Convert pictures to SVG |
| `XSLT.pm` | Apply XSLT transformation |
| `Writer.pm` | Write output files |

### 7.3 Math Post-Processing

Multiple output formats supported:
```perl
# Convert to Presentation MathML
$doc = LaTeXML::Post::MathML::Presentation->process($doc);

# Convert to Content MathML
$doc = LaTeXML::Post::MathML::Content->process($doc);

# Generate math images
$doc = LaTeXML::Post::MathImages->process($doc);
```

---

## 8. Other Notable Features

### 8.1 Constructor Pattern Language

Constructor patterns support a mini-language:

```perl
DefConstructor('\foo{}{}',
    # Basic substitution
    '<ltx:foo>#1</ltx:foo>'
    
    # Conditional
    '?#1(<ltx:a>#1</ltx:a>)(<ltx:b/>)'
    
    # Function call
    '<ltx:foo bar="&ToString(#1)"/>'
    
    # Hash unpacking for attributes
    '<ltx:foo %#1/>'
    
    # Float up tree
    '^<ltx:note>#1</ltx:note>'
);
```

### 8.2 Font Handling

LaTeXML tracks fonts throughout:

```perl
# Font properties
{ family => 'serif',
  series => 'bold', 
  shape => 'italic',
  size => 12,
  color => 'red' }

# In bindings
DefPrimitive('\textbf{}', undef, 
    font => { series => 'bold' });
```

### 8.3 Error Handling and Logging

Structured error system:

```perl
Fatal('category', 'item', $locator, "Message", @details);
Error('category', 'item', $locator, "Message");
Warn('category', 'item', $locator, "Message");
Info('category', 'item', $locator, "Message");
```

### 8.4 Profiling Support

Built-in profiling for debugging:

```perl
# Enable with --TRACEPROFILE
# Tracks time in each macro/primitive
startProfiling($cs, 'expand');  # or 'digest' or 'absorb'
stopProfiling($cs, 'expand');
```

### 8.5 Rewrite Rules

Post-construction document transformations:

```perl
# Add attributes based on XPath match
DefRewrite(
    xpath => '//ltx:ref[@idref]',
    attributes => { class => 'internal-ref' });

# Replace content
DefRewrite(
    match => '\alpha',
    replace => '\beta');

# Math ligatures  
DefMathLigature(sub {
    my ($document, @nodes) = @_;
    # Return ($n, $replacement, %attrs) or undef
});
```

### 8.6 BibTeX Integration

LaTeXML processes BibTeX files:

```bash
latexml --dest=refs.bib.xml refs.bib
latexmlpost --bibliography=refs.bib.xml doc
```

### 8.7 Daemon Mode

For high-throughput processing:

```perl
# LaTeXML can run as a daemon for repeated conversions
# Preserves loaded packages between conversions
my $converter = LaTeXML->new($config);
$converter->prepare_session($config);
my $result = $converter->convert($source);
```

---

## 9. Key Design Patterns

### 9.1 Pull-Based Processing
The Gullet pulls tokens from the Mouth, and the Stomach pulls from the Gullet. This enables lazy evaluation and proper scoping.

### 9.2 Definition Polymorphism
Different definition types (Expandable, Primitive, Constructor) share a common interface but behave differently during expansion vs digestion.

### 9.3 Whatsit Pattern
Constructors don't immediately generate XML. Instead, they create Whatsits that carry the construction instructions and arguments for later execution.

### 9.4 Document Model Abstraction
The Model/Schema system abstracts document structure, enabling automatic element opening/closing and validation.

### 9.5 Binding Layering
Package bindings can override and extend other bindings, with proper Perl module scoping in `LaTeXML::Package::Pool`.

---

## 10. File Organization

```
latexml/
├── lib/LaTeXML/
│   ├── Core/              # Core processing classes
│   │   ├── Token.pm       # Token representation
│   │   ├── Tokens.pm      # Token lists
│   │   ├── Mouth.pm       # Tokenizer
│   │   ├── Gullet.pm      # Expander
│   │   ├── Stomach.pm     # Digester
│   │   ├── Document.pm    # XML constructor
│   │   ├── State.pm       # State management
│   │   ├── Definition/    # Definition types
│   │   │   ├── Expandable.pm
│   │   │   ├── Primitive.pm
│   │   │   └── Constructor.pm
│   │   ├── Box.pm         # Digested content
│   │   ├── Whatsit.pm     # Digested constructor
│   │   └── ...
│   ├── Common/            # Shared utilities
│   │   ├── Model.pm       # Document model
│   │   ├── Font.pm        # Font handling
│   │   └── ...
│   ├── Package/           # Package bindings (~400 files)
│   │   ├── TeX.pool.ltxml
│   │   ├── LaTeX.pool.ltxml
│   │   ├── amsmath.sty.ltxml
│   │   └── ...
│   ├── Post/              # Post-processors
│   │   ├── MathML.pm
│   │   ├── Scan.pm
│   │   └── ...
│   ├── Package.pm         # Binding API exports
│   ├── MathParser.pm      # Math parser
│   └── Core.pm            # Main coordinator
└── doc/manual/            # Documentation
```

---

## 11. Comparison: LaTeXML vs Lambda LaTeX Pipeline

This section compares LaTeXML's design with Lambda's current LaTeX pipeline and proposes enhancements.

### 11.1 Current Lambda LaTeX Architecture

```
Lambda LaTeX Pipeline:

LaTeX Source
     │
     ▼
┌─────────────────────────────────┐
│  Tree-sitter LaTeX Parser       │  input-latex-ts.cpp
│  (Hybrid grammar: ~50 rules)    │  tree-sitter-latex/grammar.js
└─────────────────────────────────┘
     │
     ▼
┌─────────────────────────────────┐
│  Lambda Element AST             │  Lambda Item/Element tree
│  (Generic containers)           │
└─────────────────────────────────┘
     │
     ├──────────────────────┬────────────────────┐
     ▼                      ▼                    ▼
┌──────────────┐    ┌───────────────┐    ┌────────────────┐
│ HTML Output  │    │ TexDocModel   │    │ Direct TexNode │
│(format_latex │    │(Intermediate) │    │ (DVI/PDF/SVG)  │
│ _html.cpp)   │    │               │    │ tex_latex_     │
└──────────────┘    └───────────────┘    │ bridge.cpp     │
                                         └────────────────┘
```

**Key Files in Lambda:**
| File | Purpose |
|------|---------|
| `input-latex-ts.cpp` | Tree-sitter based LaTeX parser → Lambda Elements |
| `tex_macro.hpp/cpp` | Basic macro expansion (\def, \newcommand) |
| `tex_document_model.hpp` | Intermediate document representation |
| `tex_latex_bridge.hpp/cpp` | LaTeX Element → TexNode conversion |
| `latex_packages.hpp/cpp` | Package symbol tables (textgreek, stix, etc.) |
| `format_latex_html.cpp` | Direct LaTeX → HTML conversion |

### 11.2 Side-by-Side Comparison

| Aspect | LaTeXML | Lambda LaTeX |
|--------|---------|--------------|
| **Parser** | Custom Perl tokenizer (catcode-aware) | Tree-sitter grammar (pattern-based) |
| **Expansion** | Pull-mode Gullet with TeX semantics | MacroProcessor with simple substitution |
| **Digestion** | Stomach produces Boxes/Whatsits | Direct AST-to-output conversion |
| **IR Count** | 4 (Tokens→Boxes→Whatsits→DOM) | 2-3 (CST→Elements→TexNode/HTML) |
| **Package System** | 400+ `.ltxml` bindings with DefConstructor | Symbol tables only, no semantic bindings |
| **Scoping** | Full TeX scoping (groups, global, named) | Basic group scoping in MacroProcessor |
| **Math** | Grammar-based MathParser → XMath → MathML | Tree-sitter-latex-math → TexNode |
| **Document Model** | Schema-validated XML DOM | TexDocModel (enum-based types) |
| **Extensibility** | Perl callbacks, pattern language | C++ code changes required |

### 11.3 Key Gaps in Lambda's Current Design

1. **No Digestion Phase**: Lambda skips the crucial digestion step where LaTeXML converts commands into semantic Boxes/Whatsits with context-aware processing.

2. **Weak Macro Expansion**: `MacroProcessor` handles basic `\def`/`\newcommand` but lacks:
   - Catcode-aware tokenization
   - Conditional expansion (`\if...`)
   - `\expandafter`, `\noexpand`, `\the`
   - Proper parameter parsing with delimiters

3. **Hard-coded Commands**: Commands are handled via switch statements in conversion code, not via a binding registry.

4. **No Constructor Pattern**: LaTeXML's declarative `DefConstructor` pattern allows defining output structure without code; Lambda requires C++ implementation.

5. **Limited Package Support**: Only symbol tables, no semantic command bindings.

---

## 12. Enhancement Proposal

For detailed proposals to enhance Lambda's LaTeX pipeline based on LaTeXML's design, see:

**[Latex_Typeset_Design3.md](./Latex_Typeset_Design3.md)**

The proposal covers:
1. Full macro expansion support (catcodes, conditionals, \expandafter, etc.)
2. Extensible package binding system
3. Digestion phase with semantic IR
4. Implementation of commonly used LaTeX packages
5. Testing strategy using LaTeXML test fixtures as reference

### Summary: Key Takeaways from LaTeXML

| LaTeXML Feature | Value for Lambda | Priority |
|-----------------|------------------|----------|
| Digestion phase (Box/Whatsit) | Enables semantic processing before output | High |
| DefConstructor pattern language | Declarative, maintainable command definitions | High |
| Extensible package bindings | User-extensibility without C++ | High |
| Full TeX scoping model | Compatibility with complex documents | Medium |
| Grammar-based math parser | Semantic math structure | Medium |
| Schema-aware document model | Auto-open/close, validation | Medium |
| Post-processing chain | Modular transformations | Low |
| Named scopes | Advanced package interaction | Low |

The most impactful enhancements would be:
1. **Add a digestion phase** to capture semantics before output
2. **Implement a command registry** to replace hard-coded switch statements
3. **Create a package binding language** for extensibility
4. **Add a constructor pattern language** for declarative output generation
